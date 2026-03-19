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
SECT_CODE = "$.code"
SECT_DATA = "$.data"

## prefixes
PRE_BASE = 0b00010000 # instruction prefix
# use rshift by 2 to set the first operand
PRE_REG = 0b00 # operand is a register
PRE_INT = 0b01 # operand is an immediate integer
PRE_PTR = 0b10 # operand is a pointer

MEM_WIDTH_SUFFIXES = ("b", "w", "d")

## errors
INVALID_OPCODE = -1
INVALID_COMB = -2
OUT_OF_BOUNDS = -3

instructions: dict = json.load(open("assembler/instruction_config.json"))

# directives
D_ENTRY = "entry"
D_DEF = "def"
D_DB = "db"
D_DW = "dw"
D_DD = "dd"
D_DS = "ds"
D_SECT = "section"
D_STRUCT = "struct"
D_ESTRUCT = "end_struct"

# registers
register_list = ("a", "b", "c", "d", "r0", "sp")

lines: List[str] = []
crt_line = 1

sym_table: dict = {}
type_table: dict[str, dict] = {
    "b": {"size": 1},
    "w": {"size": 2},
    "d": {"size": 4}
}

data_section = bytearray()
entry_point: int = 0x00010000


# parse hex or decimal string to int
# internal function where we want the caller to handle the ValueError
def __parse_int(i: str) -> int:
    if type(i) == int: return i

    if i.startswith("0x"):
        return int(i, 16)
    elif i.startswith("0b"):
        return int(i, 2)
    else:
        return int(i)
    
# internal function wrapper where ValueError is handled
def parse_int(i: str) -> int:
    try:
        return __parse_int(i)
    except ValueError:
        print(f"Failed to parse integer in line {crt_line}: {i}")
        sys.exit(0)

def __val_2op(ins: str, operands: list) -> Union[instruction, Literal[-1, -2, -3]]:
    if len(operands) != 2:
        return INVALID_COMB

    ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    operand_types: list[str] = ["", ""]

    try:
        if ins == "movs":
            if type(operands[0]) != list and type(operands[1]) != list:
                return INVALID_COMB
            
            # at least one of the operands contains a list
            if type(operands[0]) != list and type(operands[1]) != list:
                return INVALID_COMB
        else:
            ret[0] = PRE_BASE
            operand_types = instructions[ins]["operands"]
            
        for i in range(2):
            if type(operands[i]) == list:
                operand_types[i] = operands[i][1]
                if operands[i][2]: # if ptr
                    operands[i] = f"[{operands[i][0]}]"
                else:
                    operands[i] = str(operands[i][0])
            else:
                if ins == "movs":
                    operand_types[i] = "int"

        if ins == "movs":
            # if both operands are a struct they need to have the same size
            if "int" not in operand_types:
                if type_table[operand_types[0]]["size"] != type_table[operand_types[1]]["size"]: # type: ignore
                    return INVALID_COMB
    except KeyError: # base instruction
        if not instructions[ins[:-1]]["variableMemoryWidth"] or ins[-1] not in MEM_WIDTH_SUFFIXES:
            return INVALID_OPCODE
      
        # +5 since the operands occupy 4 bits and the base instruction bit 1
        ret[0] = 1 << (MEM_WIDTH_SUFFIXES.index(ins[-1]) + 5)
        ins = ins[:-1]

        # an instruction with memory suffix accepts all operand types
        operand_types = ["any", "any"]

    # if an instruction has an 'int' operand, any type of pointer dereference is allowed
    ret[1] = instructions[ins]["op"]

    for i in range(2):
        # operand 1 index in ret (i = 0): n + 0 = n + 4i
        # operand 2 index in ret (i = 1): n + 4 = n + 4i

        if operands[i] in register_list:
            if operand_types[i] not in ("reg", "any"):
                return INVALID_COMB
            
            ret[2 + 4*i] = register_list.index(operands[i])
        else: # operand is not a plain register
            if operand_types[i] == "reg":
                return INVALID_COMB
            
            rshift = abs(i-1) * 2 # 2 if i = 0, 0 if i = 1

            # operand is a pointer
            if operands[i][0] == '[' and operands[i][len(operands[i])-1] == ']':
                if ret[0] == PRE_BASE: # base instructions do not allow pointer dereferences
                    return INVALID_COMB

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
            except ValueError:
                # check if operand is a register (i.e. movb a, [b])
                if operands[i] in register_list: # operand is already stripped
                    ret[2 + 4*i] = register_list.index(operands[i])
                else:
                    return INVALID_COMB

    # if the instruction contains memory operations, the first operand cannot contain an immediate integer
    if ret[0] & 0b1110_01_00 == ret[0]:
        return INVALID_COMB

    if ins == "movs":
        if ret[0] != 0b0000_11_11:
            return INVALID_COMB

        t = list(filter(lambda x: x != "int", operand_types))[0]
        ret[0] = type_table[t]["size"] # type: ignore
        if ret[0] > 255:
            warning("Referenced struct size exceeds 255 bytes.")
            ret[0] = 255
         
    return ret
    
def __val_1op(ins: str, operand: str) -> Union[instruction, Literal[-1, -2]]:
    ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

    try: # we can do this since the opcode is already validated
        if ins not in instructions:
            if not instructions[ins[:-1]]["variableMemoryWidth"] or ins[-1] not in MEM_WIDTH_SUFFIXES:
                return INVALID_OPCODE
                
            # +5 since the operands occupy 4 bits and the base instruction bit 1
            ret[0] = 1 << (MEM_WIDTH_SUFFIXES.index(ins[-1]) + 5)
            ins = ins[:-1]

            # an instruction with memory suffix accepts all operand types
            operand_types = ["any", "any"]
        else:
            operand_types = instructions[ins]["operands"]
    except KeyError: # base instruction
        ret[0] = PRE_BASE
        operand_types = instructions[ins]["operands"]

    ret[1] = instructions[ins]["op"]

    if operand in register_list:
        if operand_types[0] == "int":
            return INVALID_COMB
        
        ret[2] = register_list.index(operand)
    else:
        if operand_types[0] == "reg":
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
        i = i.strip()

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

# parse (nested) struct references
def __parse_nsref(op: str) -> list: # (address, type)
    split = op.split(".")

    if split[0] not in sym_table or sym_table[split[0]]["type"] in MEM_WIDTH_SUFFIXES:
        raise Exception(f"Symbol `{split[0]}` is not a struct")

    c_off = 0 # cumulative offset
    prev = sym_table[split[0]]["type"]
    for i in range(1, len(split)):
        # chcek if i is a struct member
        if split[i] not in type_table[prev]["fields"]:
            raise Exception(f"Symbol `{split[i]}` is not a member of the struct `{prev}`")

        # the cumulative offset is equal to the sum of all member sizes before the referenced nested struct
        for j in type_table[prev]["fields"].keys():
            if j != split[i]:
                c_off += type_table[type_table[prev]["fields"][j]["type"]]["size"] # ok bro
                continue
            break

        if i != len(split)-1:
            prev = type_table[prev]["fields"][split[i]]["type"]
            continue
        
    return [sym_table[split[0]]["addr"] + c_off, type_table[prev]["fields"][split[i]]["type"]]

# helper function for __sym_ref that resolves a symbol into an int
def __rs_term(term: str) -> list: # [address, type]
    n = 1
    if term[0] == '-':
        term = term[1:]
        n = -1

    try:
        return [__parse_int(term) * n, "d"]
    except ValueError:
        pass

    if len(term) == 3 and term[0] == '\'' and term[2] == '\'':
        return [ord(term[1]) * n, "d"]

    if "." in term:
        ref = __parse_nsref(term)
        return [ref[0] * n, ref[1]]

    if term in sym_table:
        return [sym_table[term]["addr"] * n, sym_table[term]["type"]]

    raise Exception(f"Failed to resolve symbol at line {crt_line}: `{term}`")

def __sym_ref(op: str, is_op1: bool = False) -> Union[str, instruction, list]:
    ptr = False

    # convert subtraction to addition of the opposite number to simplify operand splitting
    op = op.replace("-", "+-")
    if op[0] == '[' and op[-1] == ']':
        ptr = True
        op = op[1:-1]

    split = op.split("+")

    for i in range(len(split)):
        split[i] = split[i].strip()

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
    if is_op1 and len(split) > 1 and refs[0] != 0:
        raise Exception(f"Syntax error at line {crt_line}: symbolic expressions involving registers on the first operand are not allowed.")

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
                PRE_BASE, 0x03,             # mov
                0x04, 0x00, 0x00, 0x00,     # r0
                reg_idx, 0x00, 0x00, 0x00   # refs[1][0]
            ])
        except ValueError as e:
            # negative value exists in operand split
            i = split.index(f"-{list(refs[1])[0]}")
            split.pop(i)
            
            reg_idx = register_list.index(list(refs[1])[0])

            ret.append([
                PRE_BASE | PRE_INT, 0x03,   # mov
                0x04, 0x00, 0x00, 0x00,     # r0
                0x00, 0x00, 0x00, 0x00      # 0
            ])

            ret.append([
                PRE_BASE, 0x16,             # sub
                0x04, 0x00, 0x00, 0x00,     # r0
                reg_idx, 0x00, 0x00, 0x00   # refs[1][0]
            ])

        s = 0
        for x in split:
            r = __rs_term(x)
            if r[1] in MEM_WIDTH_SUFFIXES:
                s += r[0]
                continue
            
            raise Exception(f"Error at line {crt_line}: expected integer, got `{r[1]}` instead.")

        sh = (s >> 16) & 0xFFFF
        sl = s & 0xFFFF

        ret.append([
            PRE_BASE | PRE_INT, 0x02,   # add
            0x04, 0x00, 0x00, 0x00,     # r0
            (sh >> 8) & 0xFF,
            sh & 0xFF,
            (sl >> 8) & 0xFF,
            sl & 0xFF                   # sum(split)
        ])

        return ret
    elif refs[0] > 1:
        raise Exception(f"Syntax error at line {crt_line}: `{op}`. Can't have more than 1 runtime variable in an operation between symbols")
    else: # operand is a compile time constant
        ret = list()
        for x in split:
            r = __rs_term(x)
            if r[1] not in MEM_WIDTH_SUFFIXES and len(split) == 1:
                r.append(ptr) # (address, type, ptr)
                return r
            elif r[1] in MEM_WIDTH_SUFFIXES:
                ret.append(r[0])
                continue

            raise Exception(f"Error at line {crt_line}: expected integer, got `{r[1]}` instead.")

        if ptr:
            return f"[{sum(ret)}]"
        
        return str(sum(ret))


# handles d[x] directives
# d -> contains the entire instruction
# 0 -> success, 1 -> incorrect use, 2 -> not d[x]
def __handle_ddef(d: str, struct: str = "") -> Literal[0, 1, 2]:
    s = d.split(maxsplit=1)

    # .data must be defined before .code and d[x] cannot be used in .code
    if SECT_CODE in sym_table:
        return 1

    directive: str = ""
    data_operand: str = ""

    # d[x] <data>
    if len(s[0]) == 2 and s[0][0] == 'd' and s[0][1] in MEM_WIDTH_SUFFIXES:
        directive = s[0][0] + s[0][1]
        data_operand = s[1]
    
    # <symbol> d[x] <data>
    elif len(s[1]) >= 2 and s[1][0] == 'd' and s[1][1] in ("b", "w", "d", "s"):
        if s[0] in sym_table:
            warning(f"Duplicate definition of symbol `{s[0]}` at line {crt_line}")
        
        directive = s[1][0] + s[1][1]
        try:
            data_operand = s[1].split(maxsplit=1)[1]
        except IndexError:
            if not struct:
                print(f"No value passed to {directive} outside of a structure definition. (line {crt_line})")
                return 1
            elif directive == D_DS:
                print(f"`ds` requires a defined struct as an argument. (line {crt_line})")
                return 1
            data_operand = ""

        if not struct:
            sym_table[s[0]] = sym_table["$"].copy()

            if directive != D_DS:
                sym_table[s[0]]["type"] = directive[1]
            else:
                if data_operand in MEM_WIDTH_SUFFIXES or data_operand not in type_table:
                    print(f"Symbol `{data_operand}` is not defined.")
                    return 1
                sym_table[s[0]]["type"] = data_operand

    else: return 2

    if struct: # no initial value allowed (except ds)
        if directive != D_DS:
            if len(data_operand) != 0:
                print(f"Cannot pass an initial value to {directive} inside a structure definition.")
                return 1
        
            max_val = globals()[f"MAX_{directive[1]}"]
            type_table[struct]["fields"][s[0]] = {"off": type_table[struct]["fields"]["$last_elm"], "type": directive[1]}
            type_table[struct]["fields"]["$last_elm"] += int(log2(max_val + 1) / 8)
            return 0

        if data_operand in MEM_WIDTH_SUFFIXES or data_operand not in type_table:
            print(f"Symbol `{data_operand}` is not defined.")
            return 1
        
        # nested struct
        type_table[struct]["fields"][s[0]] = {"off": type_table[struct]["fields"]["$last_elm"], "type": data_operand}
        type_table[struct]["fields"]["$last_elm"] += type_table[data_operand]["size"]

        return 0

    if directive == D_DB and data_operand[0] == "\"" and data_operand[-1] == "\"":
        st = data_operand[1:-1]
        nbytes = len(st)

        data_section.extend(bytearray(st, encoding="ascii"))
    elif directive == D_DS:
        nbytes = type_table[data_operand]["size"]
        data_section.extend(bytearray(nbytes))
    else:
        arg = parse_int(data_operand)
        max_val = globals()[f"MAX_{directive[1]}"]
        nbytes = int(log2(max_val + 1) / 8)
        
        if arg > max_val:
            warning(f"Operand exceeds integer limit at line {crt_line}")

        data_section.extend(arg.to_bytes(nbytes, "big"))

    sym_table["$"]["addr"] += nbytes
    return 0

struct: str = ""
def parse_instruction(ins: str) -> Union[instruction, Literal[0, -1, -2], List[instruction]]:
    global struct

    s0 = ins.split(sep=";", maxsplit=1) # remove comments
    s = s0[0].split(maxsplit=1) # split between instruction and operands
    if len(s) == 0:
        return 0 # no instruction

    opcode = s[0]
    if opcode not in instructions and opcode[:-1] not in instructions:
        # parse directives
        if struct and s[0] == D_ESTRUCT:
            type_table[struct]["size"] = type_table[struct]["fields"]["$last_elm"]
            del type_table[struct]["fields"]["$last_elm"]

            struct = ""
            return 0

        d = __handle_ddef(s0[0], struct)
        if d == 0:
            return 0
        elif d == 1 or (d == 2 and struct):
            return INVALID_OPCODE
        
        if s[0] == D_STRUCT:
            if s[1] in type_table:
                print(f"Attempted redefinition of struct `{s[1]}`")
                sys.exit(0)
            
            struct = s[1]
            type_table[s[1]] = {"fields": {"$last_elm": 0}}
            return 0
        elif s[0] == D_SECT:
            if s[1] == ".data":
                if SECT_CODE in sym_table: # .data must be placed first
                    return INVALID_COMB

                # if .data is present it has a fixed address:
                # &(.data) = org + jmp <main> (10)
                sym_table[SECT_DATA] = entry_point + 0x0A
            elif s[1] == ".code":
                sym_table[SECT_CODE] = sym_table["$"]["addr"]
            else:
                print(f"Invalid section definition at line {crt_line}")
                sys.exit(0)

            return 0 # directive executed
        else: # might be <symbol> d[x]/def <data>
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

                sym_table[s[0]] = {"addr": addr, "type": "d"}
                return 0     

    # # instructions can be only placed in .code
    # if SECT_CODE not in sym_table:
    #     return INVALID_OPCODE

    try:
        operands = instructions[opcode]["operands"]
    except KeyError:
        operands = instructions[opcode[:-1]]["operands"]

    if len(operands) == 2:
        try:
            split = s[1].split(sep=',') # split the opearnds
            split[0] = split[0].strip()
            split[1] = split[1].strip()

            # handle symbol reference

            # evaluation of the first operand cannot return instructions
            split[0] = __sym_ref(split[0]) # type: ignore

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
    elif len(operands) == 1:
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
        
        ret: instruction = [0, 0, 0, 0, 0, 0, 0, 0, 0 ,0]
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
    global entry_point, sym_table, crt_line, lines

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
        print(f"No entry directive found. Defaulting to {hex(entry_point)}.")

    sym_table["$"] = {"addr": entry_point + 0x0A + 0x04, "type": "d"} # current byte pointer

    # code section parsing
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

                if SECT_CODE not in sym_table:
                    print("Cannot define a function outside of .code")
                    sys.exit(0)

                sym_table[lines[i][:-1]] = {"addr": sym_table[SECT_CODE] + n * INSTRUCTION_SIZE, "type": "d"}
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
                sym_table["$"]["addr"] += INSTRUCTION_SIZE
            else: # returned multiple instructions
                for j in ins:
                    ret.append(j) # type: ignore
                    n += 1 # instruction count
                    sym_table["$"]["addr"] += INSTRUCTION_SIZE
                
        crt_line += 1

    return ret


bios_functions = ("BIOS_print", "BIOS_sprint")
if __name__ == "__main__":
    bios = False
    if len(sys.argv) == 4 and "-bios" == sys.argv[3]:
        # if bios = True then the assembler will search for bios functions in the binary and save their addresses into a jump table
        # which will be placed at the beginning of the .data section
        bios = True

    start = time.time()

    p = parse_program(sys.argv[1])

    if bios:
        for i in sym_table:
            if i in bios_functions:
                idx = bios_functions.index(i)
                try:
                    data_section[4 * idx + 0] = (sym_table[i]["addr"] >> 0x18) & 0xFF
                    data_section[4 * idx + 1] = (sym_table[i]["addr"] >> 0x10) & 0xFF
                    data_section[4 * idx + 2] = (sym_table[i]["addr"] >> 0x08) & 0xFF
                    data_section[4 * idx + 3] = (sym_table[i]["addr"] >> 0x00) & 0xFF
                except IndexError:
                    print("Not enough bytes allocated for the bios jump table.")
                    sys.exit(0)

    p.insert(0, data_section) # type: ignore
    p.insert(0, int(sym_table[SECT_CODE]).to_bytes(4, "big")) # type: ignore

    if "main" not in sym_table:
        print(f"No main function defined")
        sys.exit(0)
    else:
        # the first instruction in the program will be `jmp main`
        jh = (sym_table["main"]["addr"] >> 16) & 0xFFFF
        jl = sym_table["main"]["addr"] & 0xFFFF

        p.insert(0, [ # type: ignore
            PRE_BASE | (PRE_INT << 2),  # op1 = int
            0x11,                       # jmp
            (jh >> 8) & 0xFF,   
            jh & 0xFF,
            (jl >> 8) & 0xFF,
            jl & 0xFF,                  # main
            0x00, 0x00, 0x00, 0x00
        ])

    delta = time.time() - start
    print(f"Assembly completed in {round(delta * 1000, 4)}ms")

    f = open(sys.argv[2], "wb")

    for i in range(len(p)): # type: ignore
        f.write(bytearray(p[i])) # type: ignore
