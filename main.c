#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x) * 1000)
#endif

#include <time.h>

#include "common.h"

#ifdef SDL_WINDOW
#include "renderer.h"
#endif

#include "macros.h"
#include "instructions.h"

#include "hwdev/hwcommon.h"
#include "hwdev/devs/timer.h"
#include "hwdev/devs/video.h"


#define IOPL (flags >> 4)


uint8_t *memory;
uint8_t *program;
hwdev_port_t hwdev_ports[MAX_DEV] = {0};
_Bool execution_complete = 0;
_Bool pending_sw_int = 0;

page_t initial_pt[INITIAL_PT_SIZE] = {0};

uint32_t idt; // interrupt descriptor table
uint32_t pta; // page table address
uint32_t ip, sp;
uint32_t registers[NREGISTERS] = {0};

uint16_t flags;


void handle_interrupt(uint8_t n, uint8_t e);

page_t *get_page(uint32_t va) {
    uint32_t idx = va / PAGE_SIZE;
    page_t *page = (page_t *)(memory + pta + idx * sizeof(page_t));

    if (!page->present) {
        handle_interrupt(E_PAGE, E_PAGE_PRESENT);
        return 0;
    }
    return page;
}

uint32_t VA_TO_PA(uint32_t va) {
    uint32_t off = va % PAGE_SIZE;
    page_t *page = get_page(va);

    return page->physical_addr + off;
}

void push(uint32_t op) {
    memory[VA_TO_PA(sp-0)] = (op >> 0x00) & 0xFF;
    memory[VA_TO_PA(sp-1)] = (op >> 0x08) & 0xFF;
    memory[VA_TO_PA(sp-2)] = (op >> 0x10) & 0xFF;
    memory[VA_TO_PA(sp-3)] = (op >> 0x18) & 0xFF;

    sp -= OPERAND_SIZE;
    registers[REGISTER_SP] -= OPERAND_SIZE;
    return;
}

void pushw(uint16_t op) {
    memory[VA_TO_PA(sp-0)] = (op >> 0x00) & 0xFF;
    memory[VA_TO_PA(sp-1)] = (op >> 0x08) & 0xFF;

    sp -= 2;
    registers[REGISTER_SP] -= 2;
    return;
}

void pop(uint32_t *dst) {
    memcpy(dst, &memory[VA_TO_PA(sp+1)], OPERAND_SIZE);
    *dst = __builtin_bswap32(*dst);

    sp += OPERAND_SIZE;
    registers[REGISTER_SP] += OPERAND_SIZE;
    return;
}

void popw(uint16_t *dst) {
    memcpy(dst, &memory[VA_TO_PA(sp+1)], 2);
    *dst = __builtin_bswap16(*dst);

    sp += 2;
    registers[REGISTER_SP] += 2;
    return;
}

void handle_interrupt(uint8_t n, uint8_t e) {
    printf("Interrupt: %d\n\tIP=0x%08x, SP=0x%08x\n\tA=0x%08x, B=0x%08x, C=0x%08x, D=0x%08x\n\tPTA=0x%08x, IDT=0x%08x\n",
        n, ip, sp, registers[0], registers[1], registers[2], registers[3], pta, idt
    );

    if ((flags & INT_FLAG) || (n <= 0x1F) || pending_sw_int) { // if interrupts are enabled or n is an exception

        uint32_t int_entry =(memory[VA_TO_PA(idt + n * INTERRUPT_HANDLER_SIZE + 0)] << 24) | \
                            (memory[VA_TO_PA(idt + n * INTERRUPT_HANDLER_SIZE + 1)] << 16) | \
                            (memory[VA_TO_PA(idt + n * INTERRUPT_HANDLER_SIZE + 2)] << 8) | \
                            (memory[VA_TO_PA(idt + n * INTERRUPT_HANDLER_SIZE + 3)] << 0);

        if (int_entry == 0x00 && n <= 0x1F) { // unhandled exception
            free(memory);
            exit(0);
        }

        if (n <= 0x1F) {
            push(e); // error code
        }

        push(ip + INSTRUCTION_SIZE);
        pushw(flags);

        ip = int_entry - INSTRUCTION_SIZE;
        return;
    }
}

void MEM_WRITEB(uint32_t addr, uint8_t src) {
    memory[VA_TO_PA(addr)] = src;
    return;
}

void MEM_WRITEW(uint32_t addr, uint16_t src) {
    memory[VA_TO_PA(addr+0)] = src >> 8;
    memory[VA_TO_PA(addr+1)] = src & 0xFF;
    return;
}

void MEM_WRITED(uint32_t addr, uint32_t src) {
    memory[VA_TO_PA(addr+0)] = src >> 24;
    memory[VA_TO_PA(addr+1)] = (src >> 16) & 0xFF;
    memory[VA_TO_PA(addr+2)] = (src >> 8) & 0xFF;
    memory[VA_TO_PA(addr+3)] = src & 0xFF;
}

uint8_t MEM_READB(uint32_t addr) {
    return memory[VA_TO_PA(addr)];
}

uint16_t MEM_READW(uint32_t addr) {
    return (memory[VA_TO_PA(addr)] << 8) | memory[VA_TO_PA(addr+1)];
}

uint32_t MEM_READD(uint32_t addr) {
    return  (memory[VA_TO_PA(addr+0)] << 24) | (memory[VA_TO_PA(addr+1)] << 16) | \
            (memory[VA_TO_PA(addr+2)] << 8) | memory[VA_TO_PA(addr+3)];
}

// fetch instruction bytes
uint8_t MEM_FETCH(uint32_t addr) {
    uint32_t off = addr % PAGE_SIZE;
    page_t *page = get_page(addr);

    if (~page->perms & PAGE_X) {
        handle_interrupt(E_PAGE, E_PAGE_PERM_FAULT);
        return 0;
    }

    return memory[page->physical_addr + off];
}

uint8_t exec() {
    sp = registers[REGISTER_SP];

    // any memory address passed to an instruction is virtual

    #ifdef DEBUG
    printf("Executing instruction: IP=0x%08x, SP=0x%08x\n", ip, sp);
    #endif

    uint8_t prefix = MEM_READB(ip + 0); // prefix or opcode high
    uint8_t opcode = MEM_READB(ip + 1); // opcode low

    uint32_t op1, op2;

    // copy 32 bit operand
    memcpy(&op1, &memory[VA_TO_PA(ip + 2)], OPERAND_SIZE);
    memcpy(&op2, &memory[VA_TO_PA(ip + 2 + OPERAND_SIZE)], OPERAND_SIZE);
    
    // invert endianness (to big endian)
    op1 = __builtin_bswap32(op1);
    op2 = __builtin_bswap32(op2);

    // register value is placed at the most significat byte of the operand
    uint16_t operand1h = (op1 >> 16) & 0xFFFF;
    uint16_t operand1l = op1 & 0xFFFF;

    uint16_t operand2h = (op2 >> 16) & 0xFFFF;
    uint16_t operand2l = op2 & 0xFFFF;

    // if operands contain registers
    uint8_t reg1 = op1 & 0xFF;
    uint8_t reg2 = op2 & 0xFF;

    uint32_t temp1;
    uint32_t temp2;

    switch (opcode) {
        // op1 = destination
        // op2 = source

        case SETF: {
            if (IOPL > 0) {
                handle_interrupt(E_PRIVILEGE, E_PRIVILEGE_IOPL | IOPL);
                break;
            }

            flags |= operand1l & 0xFF;
            return 1;
        }

        case ADD: { // 1 cycle
            SET_TMP1;
            SET_TMP2;

            uint32_t val, val2;

            switch (prefix & WIDTH_MASK) {
                case PRE_BASE: {
                    registers[reg1] += temp2;
                    return 1;
                }

                __ADD(B);
                __ADD(W);
                __ADD(D);
            }
        }

        case MOV: {
            SET_TMP1;
            SET_TMP2;

            uint32_t val;

            switch (prefix & WIDTH_MASK) {
                case PRE_BASE: { // no dereferences
                    registers[reg1] = temp2;
                    return 1;
                }

                __MOV(B);
                __MOV(W);
                __MOV(D);
            }
        }

        case CMP: { // 1 cycle
            SET_TMP2;

            flags &= ~ZERO_FLAG;
            flags &= ~EQUAL_FLAG;
            flags &= ~GREATER_FLAG;

            if (registers[reg1] == 0) {
                flags |= ZERO_FLAG;
                if (temp2 == 0) flags |= EQUAL_FLAG;
                return 1;
            }

            if (registers[reg1] > temp2) {
                flags |= GREATER_FLAG;
            } else if (registers[reg1] == temp2) {
                flags |= EQUAL_FLAG;
            }

            // reg1 < reg2
            return 1;
        }

        case JZ: { // 1 cycle
            JMP_COND_CHECK(ZERO_FLAG);
            return 1;
        }

        case JNZ: {
            JMP_NCOND_CHECK(ZERO_FLAG);
            return 1;
        }

        case JE: {
            JMP_COND_CHECK(EQUAL_FLAG);
            return 1;
        }

        case JNE: {
            JMP_NCOND_CHECK(EQUAL_FLAG);
            return 1;
        }

        case JG: {
            JMP_COND_CHECK(GREATER_FLAG);
            return 1;
        }

        case JGE: {
            JMP_COND_CHECK(EQUAL_FLAG | GREATER_FLAG);
            return 1;
        }

        case JL: {
            // none of the condition bits are set (default state)
            JMP_NCOND_CHECK(EQUAL_FLAG | GREATER_FLAG | ZERO_FLAG);
            return 1;
        }

        case JLE: {
            // we dont care if the equal bit is set as long as the others arent
            JMP_NCOND_CHECK(ZERO_FLAG | GREATER_FLAG);
            return 1;
        }

        case PUSH: { // 1 cycle
            SET_TMP1;

            push(temp1);
            return 1;
        }

        case POP: { // 1 cycle
            if (sp + OPERAND_SIZE >= INITIAL_STACK) {
                registers[REGISTER_SP] = sp + OPERAND_SIZE;
                handle_interrupt(E_OUT_OF_BOUNDS, 0); 
            }

            pop(&registers[reg1]);
            return 1;
        }

        case CALL: { // 1 cycle
            // call: jumps to instruction BUT pushes the next IP first
            SET_TMP1;

            push(ip + INSTRUCTION_SIZE); // push next instruction
            ip = temp1 - INSTRUCTION_SIZE;

            return 1;
        }

        case RET: { // 1 cycle
            if (sp + OPERAND_SIZE >= INITIAL_STACK) {
                registers[REGISTER_SP] = sp + OPERAND_SIZE;
                handle_interrupt(E_OUT_OF_BOUNDS, 0);
            }

            pop(&ip);
            ip -= INSTRUCTION_SIZE;

            return 1;
        }

        case JMP: { // 1 cycle
            // basically call without pushing the return address
            SET_TMP1;

            ip = temp1 - INSTRUCTION_SIZE;
            return 1;
        }

        case LIDT: { // 1 cycle
            SET_TMP1;

            idt = temp1;
            return 1;
        }

        case INT: { // 2 cycles
            // immediate values only
            // an interrupt can be triggered using this instruction even when IF is not set

            pending_sw_int = 1;
            handle_interrupt(operand1l & 0xFF, 0);
            return 1;
        }

        case CLI: { // 1 cycle
            if (IOPL > 1) {
                handle_interrupt(E_PRIVILEGE, E_PRIVILEGE_IOPL | IOPL);
            }

            flags &= ~INT_FLAG;
            return 1;
        }

        case STI: { // 1 cycle
            if (IOPL > 1) {
                handle_interrupt(E_PRIVILEGE, E_PRIVILEGE_IOPL | IOPL);
            }

            flags |= INT_FLAG;
            return 1;
        }

        case SUB: { // 1 cycle
            SET_TMP1;
            SET_TMP2;

            uint32_t val, val2;

            switch (prefix & WIDTH_MASK) {
                case PRE_BASE: {
                    registers[reg1] -= temp2;
                    return 1;
                }

                __SUB(B);
                __SUB(W);
                __SUB(D);
            }
        }

        case DIV: { // 1 cycle
            // divides register A by the operand
            SET_TMP1;

            if (temp1 == 0) {
                handle_interrupt(E_DIVISION, 0);
                return 1;
            }

            registers[REGISTER_D] = registers[REGISTER_A] % temp1;
            registers[REGISTER_A] = registers[REGISTER_A] / temp1;
            return 1;
        }

        case IN: {
            SET_TMP2;

            hwdev_write(operand1l, temp2);
            return 1;
        }

        case OUT: {
            uint32_t dst;
            hwdev_read(operand1l, &dst);
            registers[reg2] = dst;

            return 1;
        }

        case MUL: { // 1 cycle
            SET_TMP2;

            // operand size is ambiguous so pointer dereferences are not allowed
            registers[reg1] *= temp2;
            return 1;
        }

        case IRET: { // 2 cycles
            if (sp + 6 > INITIAL_STACK) {
                registers[REGISTER_SP] = sp + 6;
                handle_interrupt(E_OUT_OF_BOUNDS, 0);
            }

            popw(&flags);
            pop(&ip);

            ip -= INSTRUCTION_SIZE;
            return 2;
        }

        case PUSHA: { // 3 cycles
            push(registers[REGISTER_A]);
            push(registers[REGISTER_B]);
            push(registers[REGISTER_C]);
            push(registers[REGISTER_D]);

            return 3;
        }

        case POPA: { // 3 cycles
            if (sp + 4 * OPERAND_SIZE >= INITIAL_STACK) {
                // sp contains the faulting address
                // if the kernel manages to recover, the original state would have to be restored
                registers[REGISTER_SP] = sp + 4 * OPERAND_SIZE;
                handle_interrupt(E_OUT_OF_BOUNDS, 0);
            }

            pop(&registers[REGISTER_D]);
            pop(&registers[REGISTER_C]);
            pop(&registers[REGISTER_B]);
            pop(&registers[REGISTER_A]);

            return 3;
        }

        case OR: { // 1 cycle
            SET_TMP2;

            registers[reg1] |= temp2;
            return 1;
        }

        case XOR: { // 1 cycle
            SET_TMP2;

            registers[reg1] ^= temp2;
            return 1;
        }

        case NOT: { // 1 cycle
            registers[reg1] = ~registers[reg1];
            return 1;
        }

        case AND: {
            SET_TMP2;

            registers[reg1] &= temp2;
            return 1;
        }

        case LSH: {
            SET_TMP2;

            registers[reg1] <<= temp2;
            return 1;
        }

        case RSH: {
            SET_TMP2;

            registers[reg1] >>= temp2;
            return 1;
        }

        case MOVS: { // size / 2 cycles
            // guess which operand might contain a register
            if (op1 < NREGISTERS) op1 = registers[reg1];
            else if (op2 < NREGISTERS) op2 = registers[reg2];

            uint8_t size = prefix;
            
            // split struct into blocks of 4 to write faster
            uint8_t count = prefix / 4;
            uint8_t rem = prefix % 4;

            for (uint8_t i = 0; i < count; i++) {
                uint32_t src = MEM_READD(op2 + 4 * i);
                MEM_WRITED(op1 + 4 * i, src);
            }

            for (uint8_t i = 0; i < rem; i++) {
                uint8_t src = MEM_READB(op2 + 4 * count + i);
                MEM_WRITEB(op1 + 4 * count + i, src);
            }

            return size / 2;
        }

        case LPT: { // 1 cycle
            pta = VA_TO_PA(op1);
            return 1;
        }

        case MDUMP: { // op2 / 4 cycles
            printf("Memory dump at: 0x%08x", op1);
            for (int i = 0; i < op2; i++) {
                if (i % 8 == 0) {
                    printf("\n - 0x%08x: ", op1 + i);
                }
                printf("%02x ", memory[VA_TO_PA(op1 + i)]);
            }
            printf("\n");

            return op2 / 4;
        }

        case DUMP_STATE: { // 5 cycles
            printf("DUMP_STATE:\n");
            printf("\tFLAGS: 0x%04x\n", flags);
            printf("\tA: 0x%08x, B: 0x%08x\n", registers[REGISTER_A], registers[REGISTER_B]);
            printf("\tC: 0x%08x, D: 0x%08x\n", registers[REGISTER_C], registers[REGISTER_D]);
            printf("\tIP: 0x%08x, SP: 0x%08x\n", ip, sp);
            printf("\tPTA: 0x%08x, IDT: 0x%08x\n", pta, idt);

            return 5;
        }

        case END: { // 1 cycle
            execution_complete = 1;
            return 1;
        }

        default: {
            handle_interrupt(E_INVALID_OPCODE, opcode);

            free(memory);
            exit(0);
        }
    }

    return 0;
}

int main() {
    clock_t stop, start;

    memory = calloc(MEMORY_SIZE_KB, 1024);
    if (!memory) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return -1;
    }

    #ifdef SDL_WINDOW
    renderer_init();
    #endif

    page_t page;
    page.present = 1;
    page.kernel = 1;
    page.perms = 0;

    /*
        setup of initial page table (first 16 MB)

        0x00000000-0x00000FFF 4 KiB 	-   unmapped
        0x00001000-0x00020FFF 128 KiB 	rx  bios 
        0x00021000-0x00060FFF 256 KiB	rwx kernel bin
        0x00061000-0x00062FFF 8 KiB	    w   text buffer
        0x00063000-0x00063FFF 4 KiB     rwx idt
        0x00064000-0x0007FFFF 112 KiB 	rwx stack
        0x00080000-0x00FFFFFF 15872 KiB	rwx kernel memory
    */
    pta = KERNEL_DATA;
    for (uint32_t i = 1; i < INITIAL_PT_SIZE; i++) {
        page.physical_addr = i * PAGE_SIZE; // identity mapping
        if (1 <= i && i <= 32)          page.perms = PAGE_R | PAGE_X;
        else if (33 <= i && i <= 97)    page.perms = PAGE_R | PAGE_W | PAGE_X;
        else if (98 <= i && i <= 100)   page.perms = PAGE_W;
        else                            page.perms = PAGE_R | PAGE_W | PAGE_X;

        memcpy(memory + KERNEL_DATA + i * sizeof(page_t), &page, sizeof(page_t));
    }

    FILE *f = fopen("programs/bin/bios.bin", "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file\n");
        free(memory);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    program = malloc(fsize);
    fread(program, 1, fsize, f);
    fclose(f);

    for (size_t i = 0; i < fsize; i++) {
        memory[BIOS_START_ADDR + i] = program[i];
    }

    free(program);
    f = fopen("programs/bin/kernel.bin", "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file\n");
        free(memory);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    program = malloc(fsize);
    fread(program, 1, fsize, f);
    fclose(f);

    for (size_t i = 0; i < fsize; i++) {
        memory[KERNEL_START + i] = program[i];
    }
    free(program);

    // initialize devices
    hwdev_init_device(
        DEV_TIMER, 1, 4,
        timer_out, timer_in,
        timer_callback
    );

    hwdev_init_device(
        DEV_VIDEO, 1, 1,
        NULL, video_in,
        NULL
    );

    MEM_WRITED(BIOS_DATA_ADDR, MEMORY_SIZE_KB);

    // execute program
    ip = BIOS_START_ADDR;
    sp = INITIAL_STACK;

    uint8_t cycles = 0;
    uint64_t cnt = 0;

    start = clock();

    while (!execution_complete) {
        cycles = exec();
        cnt++;

        ip += INSTRUCTION_SIZE;
        if (hwdev_ports[DEV_TIMER].__en == 1) {
            hwdev_ports[DEV_TIMER].callback(cycles);
        }
    }

    double clocks_per_ms = CLOCKS_PER_SEC / 1000;

    stop = clock();
    double delta = (stop - start) / clocks_per_ms;

    printf("Instructions executed: %llu\nDelta: %lf ms\n", cnt, delta);

    free(memory);
    printf("Execution completed.\n");

    getchar();

    return 0;
}