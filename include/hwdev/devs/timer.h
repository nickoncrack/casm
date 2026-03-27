#pragma once

#include "hwdev/hwcommon.h"

#define INIT_INT_THRESH 5000 // 5000 cycles per interrupt

extern void timer_callback(uint8_t c);
extern void timer_in(uint8_t port, uint32_t dat);
extern void timer_out(uint8_t port, uint32_t *dst);