#include "common.h"
#include "hwdev/devs/timer.h"

uint32_t INTERRUPT_THRESHOLD = INIT_INT_THRESH;

static volatile uint32_t counter = 0;
static volatile uint32_t int_count = 0;
static volatile int32_t threshold = INIT_INT_THRESH;

extern uint16_t flags;
extern void handle_interrupt(uint8_t);
extern void __hwdev_tog_dev(uint8_t id);

void timer_callback(uint8_t c) {
    counter += c;
    threshold -= c;

    if (threshold <= 0) {
        threshold = INTERRUPT_THRESHOLD;
        int_count++;

        if (flags & INT_FLAG) {
            handle_interrupt(INT_TIMER);
        }
    }

    return;
}

void timer_in(uint8_t port, uint32_t dat) {
    switch (port) {
        case DEV_TIMER_TOGGLE & 0xFF: {
            __hwdev_tog_dev(DEV_TIMER);
            break;
        }

        case DEV_TIMER_SET_THRESH & 0xFF: {
            INTERRUPT_THRESHOLD = dat; // doesn't affect current cycle
            break;
        }

        default: {
            break;
        }
    }
}

void timer_out(uint8_t port, uint32_t *dst) {
    switch (port) {
        case DEV_TIMER_READ_CLK & 0xFF: {
            *dst = counter;
            break;
        }
        
        case DEV_TIMER_INT_CNT & 0xFF: {
            *dst = int_count;
            break;
        }

        case DEV_TIMER_RESET_INT_CNT & 0xFF: {
            int_count = 0;
            break;
        }

        default: {
            dst = NULL;
            break;
        }
    }

    return;
}