#pragma once

#include <SDL2/SDL.h>

void renderer_init();
void update();

void render_at(uint32_t addr);
void put_at(int row, int col, char *text, SDL_Color color);