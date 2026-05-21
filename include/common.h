#pragma once

#include <stdint.h>
#include <stddef.h>

// #define SDL_WINDOW
// #define DEBUG
#define SHOW_INT

#define MEMORY_SIZE_KB 65536 // 64 MiB (max PA = 0x03FFFFFF)
#define OPERAND_SIZE sizeof(uint32_t)
#define PAGE_SIZE 0x1000 // 4 KiB

#define INITIAL_PT_SIZE (16 * 1024 * 1024 / PAGE_SIZE) // 16 MiB = 4096 pages
#define BIOS_START_ADDR 0x1000
#define BIOS_DATA_ADDR  (BIOS_START_ADDR + 96*1024)
#define BIOS_SIZE       (128 * 1024) // 128 KiB

#define INSTRUCTION_SIZE (2 + 2 * OPERAND_SIZE)
#define NREGISTERS 6

#define KERNEL_START    0x00021000
#define KERNEL_DATA     0x00080000
#define FB_START        0x00061000
#define INITIAL_STACK   0x00080000

// text buffer dimensions
#define ROWS 37
#define COLS 88

#define INTERRUPT_HANDLER_SIZE sizeof(idt_entry_t)

#define WIDTH_MASK 0b11110000

typedef struct {
    uint32_t func;

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t reserved : 5;
    uint8_t iopl: 3;
    #else
    uint8_t iopl : 3;
    uint8_t reserved : 5;
    #endif
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint32_t physical_addr;

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t reserved : 3;
    uint8_t perms: 3;
    uint8_t kernel : 1;
    uint8_t present: 1;
    #else
    uint8_t present : 1;
    uint8_t kernel : 1;
    uint8_t perms : 3;
    uint8_t reserved : 3;
    #endif
} __attribute__((packed)) page_t;

#define PAGE_R 0x01
#define PAGE_W 0x02
#define PAGE_X 0x04

// exceptions
#define E_DIVISION          0x00 // division by zero
#define E_INVALID_OPCODE    0x01
#define E_NO_DEVICE         0x02 // invalid device
#define E_OUT_OF_BOUNDS     0x03
#define E_PRIVILEGE         0x04 // executing privileged instruction in non-privileged space
#define E_PAGE              0x05 // accessing an invalid page (not present)
#define E_ASSERTION         0x06 // assertion failure

// exception error codes
#define E_PRIVILEGE_IOPL    0b00000100 // instruction requires lower iopl (least 2 significant bits contain the faulting iopl)

#define E_PAGE_PRESENT      0x01
#define E_PAGE_PERM_FAULT   0x02

// device interrupts
#define INT_TIMER 0x20

/*
    bit 0   - zero flag
    bit 1   - equal flag
    bit 2   - greater flag
    bit 3   - interrupts enabled
    bit 4-6 - iopl, r/o for iopl > 0
*/
#define ZERO_FLAG        0x01
#define EQUAL_FLAG       0x02
#define GREATER_FLAG     0x04
#define INT_FLAG         0x08

// register operand values
#define REGISTER_A      0x00
#define REGISTER_B      0x01
#define REGISTER_C      0x02
#define REGISTER_D      0x03

#define REGISTER_R0     0x04
#define REGISTER_SP     0x05

// device identifiers
#define DEV_TIMER   0x00
#define DEV_SERIAL  0x01
#define DEV_VIDEO   0x02

// device ports
// high byte contains the device id
#define DEV_TIMER_READ_CLK      0x0000
#define DEV_TIMER_TOGGLE        0x0001
#define DEV_TIMER_INT_CNT       0x0002
#define DEV_TIMER_SET_THRESH    0x0003
#define DEV_TIMER_RESET_INT_CNT 0x0004

#define DEV_SERIAL_WRITE_BYTE   0x0100
#define DEV_SERIAL_GET_STATUS   0x0101

#define DEV_VIDEO_UPDATE_SCR    0x0200
