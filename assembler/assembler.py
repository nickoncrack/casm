import re
import sys
import time
import json

from math import log2
from logging import warning
from typing import List, Union, Literal

# constants
INSTRUCTION_SIZE = 10
OPERAND_SIZE = 4
instruction = List[int]

## max values
MAX_b = 2 ** 8  - 1
MAX_w = 2 ** 16 - 1 
MAX_d = 2 ** 32 - 1

## sections
SECT_FUNC = "$.func"
SECT_DATA = "$.data"

## prefixes
PRE_INS = 0b00010000 # instruction prefix
# use rshift by 2 to set the first operand
PRE_REG = 0b00 # operand is a register
PRE_INT = 0b01 # operand is an immediate integer
PRE_PTR = 0b10 # operand is a pointer

## errors
INVALID_OPCODE = -1
INVALID_COMB = -2
OUT_OF_BOUNDS = -3

instructions: dict = json.load(open("assembler/instruction_config.json"))
directives = ("entry", "def", "db", "dw", "db", "section")

# directives
D_ENTRY = directives[0]
D_DEF = directives[1]
D_DB = directives[2]
D_DW = directives[3]
D_DB = directives[4]
D_SECT = directives[5]

# registers
register_list = ("a", "b", "c", "d", "r0", "sp", "pta")

crt_line = 1

# parse hex or decimal string to int
# internal function where we want the caller to handle the ValueError
def __parse_int(i: str) -> int:
    if type(i) == int: return i

    if i.startswith("0x"):
        return int(i, 16)
    else:
        return int(i)
    
# internal function wrapper where ValueError is handled
def parse_int(i: str) -> int:
    try:
        return __parse_int(i)
    except ValueError:
        print(f"Failed to parse integer in line {crt_line}: {i}")
        sys.exit(0)

def __val_2op(ins: str, operands: List[str]) -> Union[instruction, Literal[-1, -2, -3]]:
    if len(operands) != 2:
        return INVALID_COMB

    ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    temp: list = ["any", "any"] # used to test special case

    # if an instruction has an 'int' operand, any type of pointer dereference is allowed
    ret[0] = PRE_INS
    ret[1] = instructions[ins]["op"]

    for i in range(2):
        # operand 1 index in ret (i = 0): n + 0 = n + 4i
        # operand 2 index in ret (i = 1): n + 4 = n + 4i

        if operands[i] in register_list:
            if instructions[ins]["operands"][i] == "int":
                return INVALID_COMB
            
            temp[i] = "reg"
            ret[2 + 4*i] = register_list.index(operands[i])
        else: # operand is not a plain register
            if instructions[ins]["operands"][i] == "reg":
                return INVALID_COMB
            
            rshift = abs(i-1) * 2 # 2 if i = 0, 0 if i = 1

            # operand is a pointer
            if operands[i][0] == '[' and operands[i][len(operands[i])-1] == ']':
                ret[0] |= PRE_PTR << rshift # a << 0 = a
                operands[i] = operands[i][1:-1]
            else:
                ret[0] |= PRE_INT << rshift
            
            # if op1 is not an integer, it could be a register (only valid for PRE_PTR)
            try:
                op = __parse_int(operands[i])
                if op > 0xFFFFFFFF:
                    warning(f"Operand exceeds integer limit ({op})")
                
                # in case PRE_PTR was set
                ret[0] |= PRE_INT << rshift

                op1h = (op >> 16) & 0xFFFF
                op1l = op & 0xFFFF

                ret[2 + 4*i] = (op1h >> 8) & 0xFF # byte 0
                ret[3 + 4*i] = op1h & 0xFF        # byte 1
                ret[4 + 4*i] = (op1l >> 8) & 0xFF # byte 2
                ret[5 + 4*i] = op1l & 0xFF        # byte 3

                temp[i] = "int"
            except ValueError:
                # check if operand is a register (i.e. movb a, [b])
                if operands[i] in register_list: # operand is already stripped
                    temp[i] = "int"
                    ret[2 + 4*i] = register_list.index(operands[i])
                else:
                    return INVALID_COMB

    # special cases
    if ret[0] & (PRE_PTR << 2) and ins == "mov":
        return INVALID_COMB # operand size is unspecified
    if ~ret[0] & (PRE_PTR << 2) and ret[0] & (PRE_INT << 2) and ins.startswith("mov"):
        return INVALID_COMB # mov[x] instructions cannot have an immediate integer as the first operand
         
    return ret
    

def __val_1op(ins: str, operand: str) -> Union[instruction, Literal[-1, -2]]:
    ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

    ret[0] = PRE_INS
    ret[1] = instructions[ins]["op"]

    if operand in register_list:
        if instructions[ins]["operands"][0] == "int":
            return INVALID_COMB
        
        ret[2] = register_list.index(operand)
    else:
        if instructions[ins]["operands"][0] == "reg":
            return INVALID_COMB

        if operand[0] == '[' and operand[-1] == ']':
            ret[0] |= PRE_PTR << 2
            operand = operand[1:-1]
        else:
            ret[0] |= PRE_INT << 2

        try:
            op = __parse_int(operand)
            if op > 0xFFFFFFFF:
                warning(f"Operand exceeds integer limit ({op})")
            
            ret[0] |= PRE_INT << 2

            oph = (op >> 16) & 0xFFFF
            opl = op & 0xFFFF

            ret[2] = (oph >> 8) & 0xFF
            ret[3] = oph & 0xFF
            ret[4] = (opl >> 8) & 0xFF
            ret[5] = opl & 0xFF
        except ValueError:
            # opcode [reg]
            if operand in register_list:
                ret[2] = register_list.index(operand)
            else:
                return INVALID_COMB
        
    return ret

# returns the number of register references and the registers referenced inside the operand
def __reg_refs(op: list) -> List: # [nrefs, regs]
    ret = [0, set()]

    for i in op:
        if i[0] == '[' and i[-1] == ']':
            t = i[1:-1]
        else:
            t = i

        if t in register_list:
            ret[0] += 1
            ret[1].add(i)
        elif t[0] == "-" and i[1:] in register_list:
            ret[0] += 1
            ret[1].add(i[1:])

    return ret


sym_table: dict = {}
data_section = bytearray()

entry_point: int = 0x00010000


# parses a symbolic reference and returns either an integer or a list of instructions if a register is involved
def __sym_ref(op: str) -> Union[str, instruction]:
    ptr = False

    # convert subtraction to addition of the opposite number to simplify operand splitting
    op = op.replace("-", "+-")
    if op[0] == '[' and op[-1] == ']':
        ptr = True
        op = op[1:-1]

    split = op.split("+")

    # convert characters to their ascii value
    for i in range(len(split)):
        if split[i][0] == '\'' and split[i][2] == '\'':
            split[i] = str(ord(split[i][1]))
        
        if len(split[i]) > 2:
            if split[i][0] == '-':
                if split[i][1] == '0' and split[i][2] == 'x':
                    split[i] = str(int(split[i], 16))
            elif split[i][0] == '0' and split[i][1] == 'x':
                split[i] = str(int(split[i], 16))

    ret = list()

    # if a register operation is involved, convert the symbolic expression into multiple instructions
    # i.e. jmp func+a+4
    # is converted to:
    # mov r0, a
    # add r0, func+4
    # jmp r0
    # where r0 is a special register used only in this kind of operation

    # assume the expression contains at most 1 register

    refs = __reg_refs(split) # [nrefs, {registers}]
    if refs[0] == 1:
        if len(split) == 1: # operand contains only a register
            if ptr:
                return f"[{list(refs[1])[0]}]"
            return list(refs[1])[0]

        # in this case, |{registers}| = 1 and contains the only referenced register
        try:
            # if positive value exists in operand split
            i = split.index(list(refs[1])[0])
            split.pop(i)

            reg_idx = register_list.index(list(refs[1])[0])

            ret.append([
                PRE_INS, 0x03,              # mov
                0x04, 0x00, 0x00, 0x00,     # r0
                reg_idx, 0x00, 0x00, 0x00   # refs[1][0]
            ])
        except ValueError:
            # negative value exists in operand split
            i = split.index(f"-{list(refs[1])[0]}")
            split.pop(i)
            
            reg_idx = register_list.index(list(refs[1])[0])

            ret.append([
                PRE_INS | PRE_INT, 0x03,    # mov
                0x04, 0x00, 0x00, 0x00,     # r0
                0x00, 0x00, 0x00, 0x00      # 0
            ])

            ret.append([
                PRE_INS, 0x16,              # sub
                0x04, 0x00, 0x00, 0x00,     # r0
                reg_idx, 0x00, 0x00, 0x00   # refs[1][0]
            ])

        # 1 line that converts symbol references to their values and string integers to integers
        s = sum([((__parse_int(x) if x[1:] not in sym_table else -sym_table[x[1:]]) if x not in sym_table else sym_table[x]) for x in split])

        # if ptr:
        #     if "$.data" in sym_table and s < sym_table["$.func"] and (s + sym_table["$.data"]) > sym_table["$.func"]:
        #         warning("")

        sh = (s >> 16) & 0xFFFF
        sl = s & 0xFFFF

        ret.append([
            PRE_INS | PRE_INT, 0x02,    # add
            0x04, 0x00, 0x00, 0x00,     # r0
            (sh >> 8) & 0xFF,
            sh & 0xFF,
            (sl >> 8) & 0xFF,
            sl & 0xFF                   # sum(split)
        ])

        return ret
    elif refs[0] > 1:
        print(f"Syntax error: `{op}`. Can't have more than 1 runtime variable in an operation between symbols")
        sys.exit(0)
    else: # operand is a compile time constant
        for i in range(len(split)):
            try:
                ret.append(__parse_int(split[i]))
            except ValueError:
                # if negative
                if split[i][0] == "-":
                    # pointers are always positive

                    if split[i][1:] in sym_table:
                        ret.append(-sym_table[split[i][1:]])
                    else:
                        print(f"{split[i][1:]} is not defined at line {crt_line}")
                        sys.exit(0)
                else:
                    t = split[i]

                    if t in sym_table:
                        ret.append(sym_table[t])
                    else:
                        print(f"{t} is not defined at line {crt_line}")
                        sys.exit(0)

        if ptr:
            return f"[{sum(ret)}]"
        
        return str(sum(ret))

    return 0
    

def parse_instruction(ins: str) -> Union[instruction, Literal[0, -1, -2], List[instruction]]:
    s0 = ins.split(sep="//") # remove comments
    s = s0[0].split(maxsplit=1) # split between instruction and operands
    if len(s) == 0:
        return 0 # no instruction
    
    opcode = s[0]

    ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0 ,0]
    
    # parse directives or return INVALID_OPCODE
    if opcode not in instructions:
        if s[1].startswith(D_DEF):
            # <symbol> def <addr>
            split = s[1].split()
            try:
                addr = __parse_int(split[1])
            except ValueError:
                # addr must strictly be an integer
                addr = int(__sym_ref(split[1])) # type: ignore

            if s[0] in sym_table:
                print(f"Duplicate definition of symbol `{s[0]}` (line {crt_line})")

            sym_table[s[0]] = addr
            return 0 # directive executed
        elif s[0] == D_SECT:
            if s[1] == ".data":
                if SECT_FUNC in sym_table: # .data must be placed first
                    return INVALID_COMB
                
                # if .data is present it has a fixed address:
                # &(.data) = org + jmp <main> (10)
                sym_table[SECT_DATA] = entry_point + 0x0A
            elif s[1] == ".func":
                sym_table[SECT_FUNC] = sym_table["$"]
            else:
                print(f"Invalid section definition at line {crt_line}")
                sys.exit(0)

            return 0
        elif s[0][0] == 'd': # might be a d[x] directive
            if s[0][1] not in ('b', 'w', 'd') or len(s) > 2:
                return INVALID_OPCODE
            
            # d[x] instructions are exclusive to .data
            if SECT_DATA in sym_table and SECT_FUNC in sym_table:
                return INVALID_OPCODE

            # db "string"
            if s[0][1] == 'b' and s[1][0] == "\"" and s[1][-1] == "\"":
                st = s[1][1:-1]
                nbytes = len(st)

                data_section.extend(bytearray(st, encoding="utf-8"))
            else:
                arg = parse_int(s[1])
                max_val = globals()[f"MAX_{s[0][1]}"]
                nbytes = int(log2(max_val + 1) / 8)

                if arg > max_val:
                    warning(f"Operand exceeds integer limit (line {crt_line})")

                # append arg into .data
                data_section.extend(arg.to_bytes(nbytes, "big"))

            sym_table["$"] += nbytes

            return 0


    # in 2 operand instruction can reference a symbol in the second operand
    if len(instructions[opcode]["operands"]) == 2:
        try:
            split = s[1].split(sep=',') # split the opearnds
            split[1] = split[1].strip()

            # handle symbol reference
            ref = __sym_ref(split[1])
            if type(ref) == list: # the second operand referenced a register (__sym_ref returned instructions)
                if split[1][0] == '[' and split[1][-1] == ']':
                    split[1] = "[r0]"
                else:
                    split[1] = "r0"
                ref.append(__val_2op(opcode, split)) # type: ignore
                
                return ref
            else:
                split[1] = ref # type: ignore
                return __val_2op(opcode, split) # type: ignore
        except IndexError:
            return INVALID_COMB
    elif len(instructions[opcode]["operands"]) == 1:
        try:
            op = s[1]
        except IndexError:
            return INVALID_COMB
        
        ref = __sym_ref(op)
        if type(ref) == list:
            if op[0] == '[' and op[-1] == ']':
                ref.append(__val_1op(opcode, "[r0]")) # type: ignore
            else:
                ref.append(__val_1op(opcode, "r0")) # type: ignore

            return ref
        else:
            return __val_1op(opcode, ref) # type: ignore
    else: # 0 operand instruction
        if len(s) != 1:
            return INVALID_COMB
        
        ret[0] = 0x00
        ret[1] = instructions[opcode]["op"]
        ret[2] = 0x00
        ret[3] = 0x00
        ret[4] = 0x00
        ret[5] = 0x00
        ret[6] = 0x00
        ret[7] = 0x00
        ret[8] = 0x00
        ret[9] = 0x00

        return ret


def parse_program(file: str) -> Union[List[instruction], Literal[0]]:
    global entry_point, sym_table, crt_line

    ret: List[instruction] = list()
    lines = open(file).read().splitlines()

    for i in range(len(lines)):
        lines[i] = lines[i].strip()

    # entry point can only be set at the beginning of the program
    if lines[0].startswith("entry"):
        s = lines[0].split()
        entry_point = parse_int(s[1])
        lines.pop(0) # remove entry directive
        crt_line += 1
    else:
        print(f"No entry directive found. Defaulting to {entry_point}.")

    sym_table["$"] = entry_point + 0x0A # current byte pointer

    # text section parsing
    n = 0 # nth instruction

    for i in range(len(lines)):
        # empty lines or comments
        if lines[i] == "" or lines[i][0] == '/' and lines[i][1] == '/':
            crt_line += 1
            continue
    
        if lines[i].endswith(":"): # function declaration
            if re.match("^[a-zA-Z_]{1}[a-zA-Z0-9_]+$", lines[i][:-1]): # match letters only
                if lines[i][:-1] in sym_table:
                    print(f"Duplicate definition of symbol `{lines[i][:-1]}` in {file}:{crt_line}")
                    sys.exit(0)

                if "$.func" not in sym_table:
                    print("Cannot define a function outside of .func")
                    sys.exit(0)

                sym_table[lines[i][:-1]] = sym_table["$.func"] + n * INSTRUCTION_SIZE
            else:
                print(f"Syntax error in {file}:{crt_line}: Invalid function name `{lines[i][:-1]}`")
                sys.exit(0)
        else:
            ins = parse_instruction(lines[i])
            if ins == INVALID_OPCODE:
                print(f"Invalid opcode in {file}:{crt_line}")
                sys.exit(0)
            elif ins == INVALID_COMB:
                print(f"Invalid combination of opcode and operands in {file}:{crt_line}")
                sys.exit(0)
            elif ins == 0:
                crt_line += 1
                continue
            
            if type(ins[0]) == int: # instruction parse returned single instruction
                ret.append(ins) # type: ignore
                n += 1
                sym_table["$"] += INSTRUCTION_SIZE
            else: # returned multiple instructions
                for i in ins:
                    ret.append(i) # type: ignore
                    n += 1 # instruction count
                    sym_table["$"] += INSTRUCTION_SIZE
                
        crt_line += 1

    return ret


if __name__ == "__main__":
    start = time.time()

    p = parse_program("assembler/test")
    p.insert(0, data_section) # type: ignore

    if "main" not in sym_table:
        print(f"No main function defined")
        sys.exit(0)
    else:
        # the first instruction in the program will be `jmp main`
        jh = (sym_table["main"] >> 16) & 0xFFFF
        jl = sym_table["main"] & 0xFFFF

        p.insert(0, [ # type: ignore
            PRE_INS | (PRE_INT << 2),   # op1 = int
            0x11,                       # jmp
            (jh >> 8) & 0xFF,   
            jh & 0xFF,
            (jl >> 8) & 0xFF,
            jl & 0xFF,                  # main
            0x00, 0x00, 0x00, 0x00
        ])

    delta = time.time() - start
    print(f"Assembly completed in {round(delta * 1000, 4)}ms")

    for i in sym_table:
        print(f"{i}: {hex(sym_table[i])}")

    f = open("assembler/out.bin", "wb")

    # align to instruction size
    # data_section.extend(bytearray((0x00,) * (INSTRUCTION_SIZE - len(data_section) % INSTRUCTION_SIZE))) # type: ignore

    for i in range(len(p)): # type: ignore
        print(f"{hex(entry_point + i * INSTRUCTION_SIZE)}: {list(map(lambda l: hex(l)[2:].zfill(2), p[i]))}") # type: ignore
        f.write(bytearray(p[i])) # type: ignore
