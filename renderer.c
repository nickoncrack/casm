#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "common.h"


SDL_Window *window;
SDL_Renderer *renderer;
TTF_Font *font;

extern uint8_t *memory;

// for 800x600 and 16x9 font, max 50 chars per line and 65 lines

void put_at(int row, int col, char *text, SDL_Color color) {
    SDL_Surface *s = TTF_RenderText_Blended(font, text, color);
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);

    SDL_Rect dst = {row * 9, col * 16, 0, 0}; // x, y, w, h
    SDL_QueryTexture(t, NULL, NULL, &dst.w, &dst.h);

    SDL_RenderCopy(renderer, t, NULL, &dst);
    SDL_DestroyTexture(t);
}

// pixel format: color, character
void render_fb() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            uint8_t ch    = memory[FB_START + (r * COLS + c) * 2 + 0];
            uint8_t color = memory[FB_START + (r * COLS + c) * 2 + 1];

            SDL_Color sdl_color;
            switch (color) {
                case 0x00: sdl_color = (SDL_Color){0, 0, 0}; break;       // black
                case 0x01: sdl_color = (SDL_Color){255, 0, 0}; break;     // red
                case 0x02: sdl_color = (SDL_Color){0, 255, 0}; break;     // green
                case 0x03: sdl_color = (SDL_Color){255, 255, 0}; break;   // yellow
                case 0x04: sdl_color = (SDL_Color){0, 0, 255}; break;     // blue
                case 0x05: sdl_color = (SDL_Color){255, 0, 255}; break;   // magenta
                case 0x06: sdl_color = (SDL_Color){0, 255, 255}; break;   // cyan
                case 0x07: sdl_color = (SDL_Color){255, 255, 255}; break; // white
                default:   sdl_color = (SDL_Color){128, 128, 128}; break; // gray
            }

            char text[2] = {ch, '\0'};
            put_at(c, r, text, sdl_color);
        }
    }
}

void update() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            TTF_CloseFont(font);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);

            TTF_Quit();
            SDL_Quit();

            free(memory);
            exit(0);
        }
    }

    SDL_RenderClear(renderer);
    render_fb();
    SDL_RenderPresent(renderer);
}

void renderer_init() {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    window = SDL_CreateWindow(
        "Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600, 0
    );

    renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED
    );
    
    font = TTF_OpenFont("font/ibmvga16x9.ttf", 16);
}