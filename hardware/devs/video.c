#include "common.h"
#include "renderer.h"

void video_in(uint8_t port, uint32_t dat) {
    switch (port) {
        case DEV_VIDEO_UPDATE_SCR & 0xFF: {
            update();
        }

        default: {
            break;
        }
    }
}