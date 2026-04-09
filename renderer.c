#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "common.h"


SDL_Window *window;
SDL_Renderer *renderer;
TTF_Font *font;
SDL_Texture *glyphs[256];
SDL_Color palette[8] = {
    {0,0,0},
    {255,0,0},
    {0,255,0},
    {255,255,0},
    {0,0,255},
    {255,0,255},
    {0,255,255},
    {255,255,255}
};

int glyph_x, glyph_y;

extern uint8_t *memory;

// for 800x600 and 16x9 font, max 50 chars per line and 65 lines

void put_at(int row, int col, char *text, uint8_t color) {
    SDL_Texture *t = glyphs[text[0]];
    SDL_Color c = palette[color];

    SDL_Rect dst = {row * glyph_x, col * glyph_y, glyph_x, glyph_y}; // x, y, w, h

    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_RenderCopy(renderer, t, NULL, &dst);
}

// pixel format: color, character
void render_fb() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            uint8_t ch    = memory[FB_START + (r * COLS + c) * 2 + 0];
            uint8_t color = memory[FB_START + (r * COLS + c) * 2 + 1];

            char text[2] = {ch, '\0'};
            put_at(c, r, text, color);
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
    TTF_SizeText(font, "A", &glyph_x, &glyph_y);

    // printable characters
    for (uint8_t i = 32; i <= 126; i++) {   
        char text[2] = {i, '\0'};

        SDL_Surface *s = TTF_RenderText_Blended(font, text, palette[7]);
        glyphs[i] = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
    }
}
