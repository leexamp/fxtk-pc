/**
 * fxtk_image_sdl.c — PC 端图片解码 (SDL2_image), 转成 24bit RGB 交给 fxtk 核心
 */
#include "fxtk_image.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>

fx_image_t *fx_image_load(const char *path)
{
    static int inited = 0;
    if (!inited) { IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG); inited = 1; }
    SDL_Surface *s = IMG_Load(path);
    if (!s) { printf("IMG_Load failed: %s (%s)\n", path, IMG_GetError()); return NULL; }
    SDL_Surface *fmt = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!fmt) return NULL;

    fx_image_t *img = fx_image_create(fmt->w, fmt->h);
    if (img) {
        uint32_t *p = (uint32_t *)fmt->pixels;
        int pitch = fmt->pitch / 4;
        for (int y = 0; y < fmt->h; y++)
            for (int x = 0; x < fmt->w; x++) {
                uint32_t c = p[y * pitch + x];
                img->px[y * img->w + x] = c & 0xFFFFFF;
            }
    }
    SDL_FreeSurface(fmt);
    return img;
}
