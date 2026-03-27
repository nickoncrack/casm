#include <stdio.h>
#include <stdlib.h>
#include "hwdev/hwcommon.h"

extern uint8_t *memory;


void hwdev_init_device(uint8_t id, uint8_t bin, uint8_t bout, void (*read)(uint8_t, uint32_t *), void (*write)(uint8_t, uint32_t), void (*callback)(uint8_t)) {
    if (id >= MAX_DEV) {
        printf("Max hardware ports exceeded.\n");
        free(memory);
        exit(-1);
    }

    // allowed parameter sizes: 1, 2, 4 bytes
    if ((bin != 1 && bin != 2 && bin != 4) || (bout != 1 && bout != 2 && bout != 4)) {
        printf("Invalid parameter size.\n");
        free(memory);
        exit(-1);
    }

    hwdev_ports[id].__en = 0;
    hwdev_ports[id].bin = bin;
    hwdev_ports[id].bout = bout;
    hwdev_ports[id].read = read;
    hwdev_ports[id].write = write;
    hwdev_ports[id].callback = callback;
    return;
}

void hwdev_read(uint16_t port, uint32_t *dst) {
    uint8_t id = (port >> 8) & 0xFF;
    uint8_t p = port & 0xFF;

    if (id >= MAX_DEV || hwdev_ports[id].read == NULL) {
        dst = NULL;
        return;
    }

    hwdev_ports[id].read(p, dst);
    return;
}

uint32_t hwdev_write(uint16_t port, uint32_t src) {
    uint8_t id = (port >> 8) & 0xFF;
    uint8_t p = port & 0xFF;

    if (id >= MAX_DEV || hwdev_ports[id].write == NULL) {
        if (src == 0) return -1;
        return 0;
    }

    hwdev_ports[id].write(p, src);
    return src;
}

void __hwdev_tog_dev(uint8_t id) {
    hwdev_ports[id].__en = (hwdev_ports[id].__en) ? 0 : 1;
}