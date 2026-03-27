#pragma once

#include "common.h"

#define MAX_DEV 64


typedef struct {
    uint8_t __en;
    uint8_t bin;
    uint8_t bout;
    void (*read)(uint8_t, uint32_t*);
    void (*write)(uint8_t, uint32_t);
    void (*callback)(uint8_t param);
} hwdev_port_t;


extern hwdev_port_t hwdev_ports[];

void hwdev_init_device(uint8_t id, uint8_t bin, uint8_t bout,
                        void (*read)(uint8_t, uint32_t*),
                        void (*write)(uint8_t, uint32_t),
                        void (*callback)(uint8_t));
void hwdev_read(uint16_t port, uint32_t *dst);
uint32_t hwdev_write(uint16_t port, uint32_t src);