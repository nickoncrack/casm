#pragma once

#define SETF        0x01 // set flags

#define ADD         0x02
#define MOV         0x03

#define CMP         0x04
#define JZ          0x05 // jump zero
#define JNZ         0x06 // jump not zero
#define JE          0x07 // jump equal
#define JNE         0x08 // jump not equal
#define JG          0x09 // jump greater
#define JGE         0x0A // jump greater or equal
#define JL          0x0B // jump less
#define JLE         0x0C // jump less or equal

#define PUSH        0x0D
#define POP         0x0E
#define CALL        0x0F
#define RET         0x10
#define JMP         0x11

#define LIDT        0x12 // load interrupt descriptor table
#define INT         0x13 // trigger interrupt
#define CLI         0x14 // clear interrupt flag
#define STI         0x15 // set interrupt flag

#define SUB         0x16
#define DIV         0x17

#define IN          0x18
#define OUT         0x19

#define MUL         0x1A
#define IRET        0x1B // interrupt return

#define PUSHA       0x1C // push all
#define POPA        0x1D // pop all

#define OR          0x1E
#define XOR         0x1F // exclusive or
#define NOT         0x20
#define AND         0x21
#define LSH         0x22 // left shift
#define RSH         0x23 // right shift

#define MOVS        0x24 // move struct
#define LPT         0x25 // load page table

#define SZ          0x26 // assert zero
#define SNZ         0x27 // assert not zero
#define SE          0x28 // assert equal
#define SNE         0x29 // assert not equal
#define SG          0x2A // assert greater
#define SGE         0x2B // assert greater or equal
#define SL          0x2C // assert less
#define SLE         0x2D // assert less or equal

// special instructions
#define MDUMP       0xFD
#define DUMP_STATE  0xFE
#define END         0xFF

// instruction prefixes
#define PRE_BASE    0b00010000
#define PRE_BYTE    0b00100000
#define PRE_WORD    0b01000000
#define PRE_DWORD   0b10000000
#define PRE_INT1    0b0100
#define PRE_PTR1    0b1000
#define PRE_INT2    0b01
#define PRE_PTR2    0b10
