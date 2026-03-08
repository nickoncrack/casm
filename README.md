# casm
A hardware program that works on a homemade instruction set

## Documentation
### 0. Definitions

#### 0a. General architecture
+ All general purpose registers, and special registers (except `flags`) are 32 bits wide.
+ **Fixed instruction length**: All instructions are 10 bytes long.
+ **Paging**: Paging is always enabled. Page size is fixed to 4 KiB.
+ **Privilege levels**: Controlled by the 3 bit `IOPL` field in the `flags` register (0 = BIOS, 1 = kernel, higher = user)
+ On `INT n`, if current_IOPL > required_caller_IOPL, a privilege exception is raised before state modification.
+ The virtual address of the page table across all address spaces is the same.

#### 0b. Notation
+ `op1, op2`: Operands of an instruction
+ `[addr]`: Memory dereference
+ `addr+10`: Symbolic reference (strictly in bytes)
+ `instruction[x]`: Any width variant
+ In this document a semicolon is used to denote a comment inside a code block, in to-be-assembled code `//` must be used instead.

#### 0c. Data types
| Type | Width | Range |
| --- | --- | --- |
| byte | 8 bit | 0-255 |
| word | 16 bit | 0-65,535 |
| dword | 32 bit | 0-4,294,967,295 |

+ All instructions involving a memory operation must explicitly specify width (`movb`, `movw`, `movd`, etc.)
+ Instructions without an explicit width that attempt memory dereference cause an invalid opcode exception.
+ If an operand exceeds the maximum value of its data type, it will be replaced by that maximum value

#### 0d. Endianness
+ **Memory**: big endian
+ **Instruction operands**: big endian (see section `2f` for exact byte layout)
+ **Registers**: endian neutral
+ In this document `bit 0` refers to the least significant bit.

#### 0e. Stack
+ Grows downwards (stack pointer is decremented on push)
+ All of the following operations: `push`, `pop`, `call`, `ret` are dword by default 

### 1. Registers
The available registers are:
+ `a, b, c, d`, which are the standard 32-bit registers and are all modifiable using the `mov` instructions
+ `r0`, a special register, the use of which will be explained in section `2c`
+ `ip`, points to the first byte of the current instruction
+ `sp`, the stack pointer which is modifiable using the `mov` instructions
+ `flags`, contains various flags about the current instruction, can be modified using `setf` or other instructions (only at `iopl == 0x00`)

As explained below, registers are passed as numbers in the high byte of an operand.
The following table contains the number corresponding to each register that is modifiable by the `mov` instructions
| Value | Register |
| --- | --- |
| 0x00 | Register A |
| 0x01 | Register B |
| 0x02 | Register C |
| 0x03 | Register D |
| 0x04 | Register `r0` |
| 0x05 | Stack pointer (`sp`) |

+ `ip` and `flags` are not directly accessible via `mov`.

#### 1a. The `flags` register
```
 15          6      3                0
+----------+------+----+----+----+----+
| Reserved | IOPL | IF | GF | EF | ZF |
+----------+------+----+----+----+----+
```

| Flag | Length | Description |
| --- | --- | --- |
| ZF | 1 |  Zero flag, set by `cmp` when the register in the first operand is equal to 0. |
| EF | 1 | Equal flag, set by `cmp` when `op1 = op2` |
| GF | 1 | Greater flag, set by `cmp` when `op1 > op2` |
| IF | 1 | Interrupt flag; if set, interrupts are enabled. Can be toggled using `cli` and `sti` |
| IOPL | 3 | Current I/O privilege level, readonly for `iopl > 0x00` |

#### 1b. Processor state on kernel entry
+ `flags` = `0x0010` (iopl = 1)
+ `ip` = `0x00010000`
+ `sp` = `0x00FFFFFF`
+ `pta` = `0x00081000`
+ `a,b,c,d` = `0x00000000`

### 2. Assembler
+ The assembler parses the code line by line
+ If a symbolic reference is found, i.e. a function (`func`), an operation between symbols and registers (`func+a+10`), the assembler will call `__sym_ref()`
+ `__sym_ref()` will simplify the symbolic reference by replacing the value corresponding to the symbol or by splitting the operation into multiple instructions if a register is involved<br>

#### 2a. Directives
Directives are pseudo-instructions that are executed by the assembler during compile time. The following directives are currently implemented:

+ `entry <addr>`: Sets the program entry point, defaulting to `0x00010000`. Can be only placed on the first line of the program.
+ `section <.data/.code>`: Used to define a program section. `.data` section contains preallocated variables and `.code` contains the code of the program
+ `<symbol> def <addr>`: Assignes the given address to the given symbol in the symbol table (i.e. `sym_table[symbol] = addr`).
+ `(<symbol>) d[x] <value>`: Allocates a number of bytes, depending on `x` (`db`, `dw`, etc.), and assignes a value to them, cannot be used in the `.code` section. The assembler supports ASCII string definitions using `db`.
+ `(<symbol>) ds <value>`: Allocates a number of bytes, depending on the size of the referenced struct and assignes that struct as the symbol's data type. 
+ `struct <name>`: Defines a struct (section `2d`)
+ `end_struct`: Ends the definition of the struct.

#### 2b. Predefined symbols
| Symbol | Description |
| --- | --- |
| `$` | Address index pointer, i.e. points to the current instruction or index in `.data` |
| `$.data` | Points to the start of `.data` (used internally) |
| `$.code` | Points to the start of `.code` (used internally) |

+ These symbols are the only ones in the assembler's symbol table that have no data type.

Usage:
```asm
section .data
  dd 0x00
  dw 0x00
  test def $ - 0x06 ; size is ambiguous

section .code
main:
  movd a, [test] ; mov[x] instructions fix the ambiguity
```
<br>

However, the following problem arises: If a symbol is defined at the end of `.data` and the bytes allocated to it are less than the bytes invoked by `mov[x]`, the instruction will also read additional bytes from the next instruction which is essentially irrelevant to the value of the symbol. Temporarily, the assembler will show the warning:
```
4 byte read from symbol defined in section .data of size 2
```
This issue will be later addressed using page permissions which will throw a page fault on these incorrect use cases.

#### 2c. Symbolic reference parsing
```asm
func:
    does something

main:
    mov a, 12
    jmp func+a+10
```

The instruction `jmp func+a+10` is converted to:

```asm
mov r0, a
add r0, func+10
jmp r0
```
where `r0` is a special register that is used in this kind of operations. It can be used by the programmer as a general purpose register but this kind of use is non-standard.

+ Symbolic expressions that also contain registers on the first operand are illegal.
+ Each external symbol has a data type. The default type identifiers are identical to the memory width suffixes.

#### 2d. Structs
A struct is a custom data type that can be defined in `.data` in the following way:
```asm
section .data
struct st
    a db
    b db
    c dd
end_struct

value ds st
```

+ It is obvious that the members of a struct use the partial syntax of `d[x]` (except `ds`) directives as they do not have an assigned value. If a value is assigned to a struct member, an error will be thrown.
+ Nested structs are allowed:
```asm
section .data
struct st2
    a db
    b dw
    c ds st
end_struct

value2 ds st2
```

+ The maximum size of a struct is 255 bytes. (section `3b`)
+ Struct members can be accessed using a dot and then the member's identifier, just like most lnguages: `value2.c.a`
+ This allows for $n$ nested structs: $\textnormal{value}\rightarrow s_1\rightarrow s_2\rightarrow\cdots\rightarrow s_n$
+ Memory operations with structs are allowed using the `movs` instruction:
```asm
movs [st], [0xFFFFF] ; read struct from memory
movs [0xFFFFF], [st] ; write struct to memory
```

#### 2e. Stages of the assembly
Suppose we have the following program (which is unnecessarily *complicated* for the sake of explaining the inner workings of the assembler):

```asm
entry 0x00001000

end:
    dump
    end

loop:
    end ; this is never executed since we jump to loop+10, it's just a placeholder for testing addition between symbols and integers
    add a, 1
    cmp a, 10
    je end
    jl loop+10

main:
    mov a, 0
    mov b, 10 ; the instruction size is fixed to 10 bytes
    jmp loop+b
```

The assembler will first convert it to:

```asm
jmp 0x1050
dump
end
end
add a, 1
cmp a, 10
je 0x100A
jl 0x1028
mov a, 0
mov b, 10
mov r0, b
add r0, 0x101E
jmp r0
```

This snippet, although it's assembly, it isn't easily readable since the symbols have been replaced with their corresponding addresses and inline operations have been expanded into multiple instructions. This *first stage* of the assembly is internal and not outputted by the assembler, however, these instructions are ready to be directly converted into machine code, which is the second (and final) stage of the assembly.


#### 2f. Machine code
Using the following table I will explain how the machine code is generated by splitting an instruction into 3 parts:

1. The instruction prefix
2. The opcode
3. The instruction operands

```asm
addr     op    operand 1   operand 2      <sym+i>     instruction
0x1000 | 14 11 00 00 10 30 00 00 00 00                jmp <main+0>
0x100A | 00 FE 00 00 00 00 00 00 00 00    <end+0>     dump
0x1014 | 00 FF 00 00 00 00 00 00 00 00    <end+1>     end
0x101E | 00 FF 00 00 00 00 00 00 00 00    <loop+0>    end
0x1028 | 11 02 00 00 00 00 00 00 00 01    <loop+1>    add a, 1
0x1032 | 11 04 00 00 00 00 00 00 00 0A    <loop+2>    cmp a, 10
0x103C | 14 07 00 00 10 06 00 00 00 00    <loop+3>    je <end+0>
0x1046 | 14 0B 00 00 10 18 00 00 00 00    <loop+4>    jl <loop+1>
0x1050 | 11 03 00 00 00 00 00 00 00 00    <main+0>    mov a, 0
0x105A | 11 03 01 00 00 00 00 00 00 0A    <main+1>    mov b, 10
0x1064 | 10 03 04 00 00 00 01 00 00 00    <main+2>    mov r0, b
0x106E | 11 02 04 00 00 00 00 00 10 02    <main+3>    add r0, <loop+0>
0x1078 | 10 11 04 00 00 00 00 00 00 00    <main+4>    jmp r0
```
<sub>Note: Here `<sym+i>` represents the *i*th instruction, unlike symbolic addition (`func+10`) which respresents bytes.</sub>

1. The instruction prefix is a single byte that is placed before the opcode and is used to give information to the processor about the operands of the current instruction. (corresponds to the first byte of the `op` column)
<br>
The prefix itself, consists of 3 sections (does **not** apply for `movs`, see section `3b`):


```
 8      4     2    0
+------+-----+-----+
| Flag | op1 | op2 |
+------+-----+-----+
```

**i.** Flag
+ If a single bit is set, then that bit defines the memory width for the instruction. (bit 0 = `dword`, bit 1 = `word`, bit 2 = `byte`, bit 3 = base)
  For example, the prefix of `movd a, [0xFFFF]` would be `0b1000 00 11`
<br>

**ii.** Operands
+ `0b00`: register
+ `0b01`: integer
+ `0b10`: pointer dereference of a register
+ `0b11`: pointer dereference of fixed address
<br>

2. The opcode, which is the second byte in an instruction, tells the processor which instruction we want to execute. The opcode list can be found in section `3a`
3. The operands occupy the rest of the available bytes in our 10 byte instruction with each operand occupying 4 bytes. Each operand can either contain a register or an immediate value (as explained above). If an operand contains a register, then its most significant byte will contain a number which corresponds to the register.
<br>

Notice how the instruction at address `0x1000` which is `jmp <main+0>` (or `jmp 0x1050` if you like) doesn't exist in the initial program. This is used to simplify the assembly process. The instruction `entry 0x1000` doesn't *really* set the entry point (beginning of `main`) to `0x1000`, instead, it tells the assembler that the entire program will be placed at that address. Since the code is parsed line by line, it means that if the main function is first, it can't make any symbol references, as almost all other symbols will be defined after the main function. Therefore, once the assembly is completed, the assembler finds the address of the first instruction of `main` and places a jump instruction to that address at the top of the program.
<br>

### 3. Instructions
#### 3a. Normal instructions
| Opcode | Mnemonic | Description |
| --- | --- | --- |
| 0x00 | NOP | No instruction |
| 0x01 | SETF | Modifies the `flags` register according to the operation: `flags \|= op1` (only usable for `iopl == 0x00`) |
| 0x02 | ADD | Addition between the 2 operands: `op1 += op2` |
| 0x03 | MOV | Moves the second operand to the first: `op1 <- op2` |
| 0x04 | CMP | Compares the first operand with the second and sets the `flags` register accordingly |
| 0x05 | JZ | Jumps to `op1` if the zero bit is set in `flags` |
| 0x06 | JNZ | Jumps to `op1` if the zero bit is __not__ set in `flags` |
| 0x07 | JE | Jumps to `op1` if the equal bit is set |
| 0x08 | JNE | Jumps to `op1` if the equal bit is __not__ set |
| 0x09 | JG | Jumps to `op1` if the greater bit is set |
| 0x0A | JGE | Jumps to `op1` if the greater or the equal bit is set |
| 0x0B | JL | Jumps to `op1` if none of the comparison bits are set |
| 0x0C | JLE | Jumps to `op1` if none of the comparison bits are set or if the equal bit is set |
| 0x0D | PUSH | Pushes `op1` to the stack |
| 0x0E | POP | Pops the last stack value into `op1`. `op1` cannot be an immediate integer |
| 0x0F | CALL | Pushes the current `ip` into the stack and jumps to `op1` |
| 0x10 | RET | Pops `ip` |
| 0x11 | JMP | Jumps to `op1` |
| 0x12 | LIDT | Loads an interrupt descriptor table from a given memory address |
| 0x13 | INT | Triggers an interrupt, even when interrupts are *disabled* |
| 0x14 | CLI | Clear interrupt flag; disables the interrupts |
| 0x15 | STI | Set interrupt flag; enables the interrupts |
| 0x16 | SUB | Subtraction between the 2 operands: `op1 -= op2` |
| 0x17 | DIV | Divides register A by `op1`, stores the quotient in register A and the remainder in register D. If `op1` is 0, `INT 0` will be triggered |
| 0x1A | MUL | Multiplies `op1` by `op2` |
| 0x1B | IRET | Interrupt return; used when the invoked interrupt finishes execution. Pops `flags` and `ip` |
| 0x1C | PUSHA | Pushes all general purpose registers in the following order: `a, b, c, d` | 
| 0x1D | POPA | Pops the first 16 bytes of the stack into the general purpose registers in the following order: `d, c, b, a` |
| 0x24 | MOVS | Reads/writes a struct from memory (section `3b`) |

Note: Any instruction involving memory operations must encode width specifically (i.e. `mov` doesn't allow memory operations, `movd` does)

#### 3b. Special instructions
| Opcode | Mnemonic | Description |
| --- | --- | --- |
| 0xFD | MDUMP | Prints the specified number of bytes at the specified address into the terminal |
| 0xFE | DUMP | Debug instruction; dumps the current state of the processor to the terminal |
| 0xFF | END | Marks the end of the program execution |
<br>

+ The `movs` instruction is separate from `mov[x]` because it works differently. `movs` is the only instruction with a non fixed memory width as it depends on the referenced struct and it also has a fixed syntax:
```asm
movs [int], [int] ; registers are not allowed
```
Consequently, the standard prefix is redundant. However, in order to address the problem caused by variable memory width the prefix will be repurposed and for `movs` **only**, the prefix will contain the struct size, therefore, the maximum struct size is 255 bytes.
<br>

### 4. Interrupts
#### 4a. Interrupt descriptor table
An interrupt descriptor table can be loaded using the instruction `lidt <addr>`.
<br>

The structure of the interrupt descriptor table is simple, each entry has 1 element which is the address of the *n*th interrupt handler.
<br>

#### 4b. Interrupt handling
If `int n` is executed and the value of the *n*th entry in the IDT is nonzero then, `ip` will be set to `idt[n]`, after pushing the current `ip` to the stack. However, if `idt[n]` is zero, the interrupt handler will recurse to itself indefinitely, marking an unhandled interrupt. Exectuting `int 0x80` will call a BIOS function. 
<br>

Interrupt handling example:
```asm
test:
    dump
    iret

main:
    lidt 0x00F85000
    movd [0x00F853FC], test
    sti
    int 0xFF ; test will be called
```
<br>

#### 4c. Interrupts/Exceptions
| `INT n` | Type | Description | 
| --- | --- | --- |
| 0x00 | E | Division by zero |
| 0x01 | E | Invalid opcode |
| 0x02 | E | Invalid device |
| 0x03 | E | Out of bounds exception |
| 0x04 | E | Privilege exception |
| 0x05 | E | Page fault |
| 0x20 | I | Timer interrupt |
| 0x80 | I | BIOS interrupt |
<br>

### 5. Memory
#### 5a. Paging
Paging is architecturally enabled at reset with an identity mapping of the first 16 MB. The initial page table is modifiable and replacable by the BIOS and the kernel, it is not a permanent structure and therefore should be discarded by the kernel. A permanent and expanded PT must be installed by the kernel using the standard mappings (not yet implemented). Page with index `0` (`VA = 0x00000000`) should not be mapped as it can be used for null pointer exceptions, and the pages of the region where the BIOS code and the PT (bootstrap PT) live would be unpageable, meaning they are inacessible by privilege levels lower than the kernel and they can never be evicted.
<br>

The following page strucutre is used
```c
struct page {
  uint32_t physical_addr;
  uint8_t present : 1;
  uint8_t kernel : 1;
  uint8_t perms : 3;
  uint8_t reserved : 3;
};
```

<br>
Page permissions:

```
 2   1   0
+---+---+---+
| X | W | R |
+---+---+---+
```
- X: Execute
- W: Write
- R: Read

Note: Here X does not imply R, architecturally it does since its the same operation, but a page with X and not R cannot be read from the kernel using the `mov` instructions

#### 5b. Initial memory structure
This memory structure is temporary and is set up by the CPU after reset. It is standard practice that the kernel copies these mappings into the expanded page table and leave them as is.
<br>
| Start VA | Page index | Size | Permissions | Description |
| --- | --- | --- | --- | --- |
| `0x00000000` | 0 | 4 KiB | - | Unmapped page for detecting null pointer dereferences |
| `0x00001000` | 1-128 | 512 KiB | RX | BIOS region (does not contain only the binary) |
| `0x00081000` | 129-134 | 20 KiB | RW | Initial page table |
| `0x00087000` | 135-2048 | ~7.47 MiB | X | Kernel binary |
| `0x00801000` | 2049-3969 | 7.5 MiB | RWX | Reserved kernel memory |
| `0x00F82000` | 3970-3972 | 8 KiB | W | Text video buffer |
| `0x00F85000` | 3973 | 4 KiB | RWX | Interrupt descriptor table |
| `0x00F86000` | 3974-3999 | 100 KiB | RWX | Reserved |
| `0x00FA0000` | 4000-4096 | 384 KiB | RW | Stack |
<br>

### 6. I/O Ports
The processor communicates with external hardware devices using the `in` and `out` instructions (i.e. `inb`, `outb`)

| Port | Device | Description |
| --- | --- | --- |
| `0x00` | System timer | Provides high resolution timing and interrupt generation |

#### 6a. System timer commands
- **0x0000 (READ_CLK):** Returns the current 32-bit cycle counter.
- **0x0001 (TOGGLE):** Enables/Disables the counter.
- **0x0002 (INT_CNT):** Returns the number of timer interrupts triggered since reset.
- **0x0003 (SET_THRESH):** Sets the cycle threshold for the next `INT 0x20`.
- **0x0004 (RESET_INT_CNT):** Resets the number of interrupts triggered.
