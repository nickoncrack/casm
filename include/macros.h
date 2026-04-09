#pragma once

/* commonly used macros */
#define __B PRE_BYTE // defined in common.h
#define __W PRE_WORD
#define __D PRE_DWORD

#define _B 1
#define _W 2
#define _D 4

#define __MASK_B 0xFF
#define __MASK_W 0xFFFF
#define __MASK_D 0xFFFFFFFF

#define SET_TMP1 \
    if (prefix & PRE_INT1) temp1 = op1; \
    else temp1 = registers[reg1];

#define SET_TMP2 \
    if (prefix & PRE_INT2) temp2 = op2; \
    else temp2 = registers[reg2];


// conditional jump check condition
#define JMP_COND_CHECK(cond) \
    SET_TMP1; \
    if (flags & (cond)) { \
        if (!(prefix & PRE_PTR1)) { \
            ip = temp1 - INSTRUCTION_SIZE; \
            return 1; \
        } \
        uint32_t ret = MEM_READD(temp1); \
        ip = ret - INSTRUCTION_SIZE; \
    }

// conditional jump check negated condition
#define JMP_NCOND_CHECK(cond) \
    SET_TMP1; \
    if (~flags & (cond)) { \
        if (!(prefix & PRE_PTR1)) { \
            ip = temp1 - INSTRUCTION_SIZE; \
            return 1; \
        } \
        uint32_t ret = MEM_READD(temp1); \
        ip = ret - INSTRUCTION_SIZE; \
    }

#define ASSERT_COND_CHECK(cond) \
    if (flags & (cond)) return 1; \
    else handle_interrupt(E_ASSERTION, 0);  

#define ASSERT_NCOND_CHECK(cond) \
    if (~flags & (cond)) return 1; \
    else handle_interrupt(E_ASSERTION, 0);

/* 
    instruction macros

    all instructions with memory width variants will have their own macro to reduce repetition
*/
#define __MOV(WIDTH) \
    case __##WIDTH: { \
        if (prefix & PRE_PTR2) val = MEM_READ##WIDTH(temp2); \
        else val = temp2 & __MASK_##WIDTH; \
        if (prefix & PRE_PTR1) MEM_WRITE##WIDTH(temp1, val); \
        else registers[reg1] = val; \
        return 2; \
    }

#define __PUSH(WIDTH) \
    case __##WIDTH: { \
        if (prefix & PRE_PTR1) val = MEM_READ##WIDTH(temp1); \
        else val = temp1 & __MASK_##WIDTH; \
        PUSH##WIDTH(temp1); \
        return 2; \
    }

#define __POP(WIDTH) \
    case __##WIDTH: { \
        if (sp + _##WIDTH >= INITIAL_STACK) { \
            registers[REGISTER_SP] = sp + _##WIDTH; \
            handle_interrupt(E_OUT_OF_BOUNDS, 0); \
        } \
        POP##WIDTH(&registers[reg1]); \
        return 1; \
    }

#define __ADD(WIDTH) \
    case __##WIDTH: { \
        if (prefix & PRE_PTR2) val = MEM_READ##WIDTH(temp2); \
        else val = temp2 & __MASK_##WIDTH; \
        if (prefix & PRE_PTR1) { \
            val2 = MEM_READ##WIDTH(temp1); \
            MEM_WRITE##WIDTH(temp1, val+val2); \
            return 2; \
        } \
        registers[reg1] += val; \
        return 1; \
    }

#define __SUB(WIDTH) \
    case __##WIDTH: { \
        if (prefix & PRE_PTR2) val = MEM_READ##WIDTH(temp2); \
        else val = temp2 & __MASK_##WIDTH; \
        if (prefix & PRE_PTR1) { \
            val2 = MEM_READ##WIDTH(temp1); \
            MEM_WRITE##WIDTH(temp1, val-val2); \
            return 2; \
        } \
        registers[reg1] -= val; \
        return 1; \
    }
