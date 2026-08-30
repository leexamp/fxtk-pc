/**
 * fxtk_font.c — v2 极致性能版: 文字 GPU 纹理缓存 (零 CPU 上传)
 */
#define _POSIX_C_SOURCE 200809L   /* 让 <string.h> 暴露 strdup (严格 -std 下会隐式声明) */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "fxtk.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern SDL_Renderer *fxtk_get_sdl_renderer(void);
extern void fxtk_flush_driver_batch(void);
extern int fxtk_is_offing(void);
extern void fxtk_text_blit(void *tex,int x,int y,int w,int h);
extern void fxtk_put_px(int x, int y, uint16_t c);

#define TEXT_CACHE_SIZE 256
typedef struct {
    TTF_Font *font;
    char *key;
    int w, h;
    SDL_Texture *tex;
    uint32_t age;
} tc_entry_t;
static tc_entry_t s_tc[TEXT_CACHE_SIZE];
static uint32_t s_tc_clock = 0;

static tc_entry_t *tc_lookup(TTF_Font *font, const char *s, fx_color_t fg, fx_color_t bg)
{
    char key[256];
    snprintf(key, sizeof(key), "%s|%06x|%06x", s, (unsigned)fg, (unsigned)bg);
    for (int i = 0; i < TEXT_CACHE_SIZE; i++) {
        if (s_tc[i].key && s_tc[i].font == font && strcmp(s_tc[i].key, key) == 0) {
            s_tc[i].age = ++s_tc_clock;
            return &s_tc[i];
        }
    }
    return NULL;
}
static tc_entry_t *tc_evict(void)
{
    for (int i = 0; i < TEXT_CACHE_SIZE; i++)
        if (!s_tc[i].key) return &s_tc[i];
    int min_i = 0;
    for (int i = 1; i < TEXT_CACHE_SIZE; i++)
        if (s_tc[i].age < s_tc[min_i].age) min_i = i;
    free(s_tc[min_i].key);
    if (s_tc[min_i].tex) SDL_DestroyTexture(s_tc[min_i].tex);
    s_tc[min_i].key = NULL;
    s_tc[min_i].tex = NULL;
    return &s_tc[min_i];
}

static TTF_Font *g_font = NULL;
static int g_font_size = 16;
static char s_font_path[160];

void fxtk_font_init(const char *unused_path, int size)
{
    if (TTF_Init() == -1) {
        printf("TTF_Init failed: %s\n", TTF_GetError());
        return;
    }
    const char *env = getenv("FXTK_FONT");
    const char *fb[] = {
        env,
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (int i = 0; i < (int)(sizeof(fb)/sizeof(fb[0])); i++) {
        if (!fb[i]) continue;
        g_font = TTF_OpenFont(fb[i], size);
        if (g_font) {
            snprintf(s_font_path, sizeof(s_font_path), "%s", fb[i]);
            printf("[font] SUCCESS: %s (h=%d)\n", s_font_path, TTF_FontHeight(g_font));
            g_font_size = size;
            return;
        }
    }
    printf("[font] ALL FAILED!\n");
}

int fx_text_width(const char *s)
{
    if (!g_font || !s) return 0;
    int w, h;
    TTF_SizeUTF8(g_font, s, &w, &h);
    return w;
}

void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg)
{
    if (!g_font || !s) return;
    SDL_Renderer *ren = fxtk_get_sdl_renderer();
    if (!ren) return;

    tc_entry_t *hit = tc_lookup(g_font, s, fg, bg);
    if (hit && hit->tex && !fxtk_is_offing()) {
        fxtk_text_blit(hit->tex, x, y, hit->w, hit->h);   /* 偏移+裁剪 */
        return;
    }

    /* 24bit RGB 直接取通道 */
    uint8_t r_fg = (uint8_t)((fg >> 16) & 0xFF);
    uint8_t g_fg = (uint8_t)((fg >> 8) & 0xFF);
    uint8_t b_fg = (uint8_t)(fg & 0xFF);
    SDL_Color c_fg = {r_fg, g_fg, b_fg, 255};

    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, s, c_fg);
    if (!surf) return;

    SDL_Surface *fmt_surf = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(surf);
    if (!fmt_surf) return;

    uint8_t r_bg = (uint8_t)((bg >> 16) & 0xFF);
    uint8_t g_bg = (uint8_t)((bg >> 8) & 0xFF);
    uint8_t b_bg = (uint8_t)(bg & 0xFF);

    uint32_t *src_px = (uint32_t *)fmt_surf->pixels;
    int w = fmt_surf->w;
    int h = fmt_surf->h;
    int pitch = fmt_surf->pitch / 4;

    uint32_t *argb_buf = (uint32_t *)malloc((size_t)w * h * 4);
    if (argb_buf) {
        for (int ty = 0; ty < h; ty++) {
            for (int tx = 0; tx < w; tx++) {
                uint32_t p = src_px[ty * pitch + tx];
                uint8_t alpha = (p >> 24) & 0xFF;
                uint8_t r = (p >> 16) & 0xFF;
                uint8_t g = (p >> 8) & 0xFF;
                uint8_t b = p & 0xFF;
                uint8_t fr = (r * alpha + r_bg * (255 - alpha)) / 255;
                uint8_t fg_v = (g * alpha + g_bg * (255 - alpha)) / 255;
                uint8_t fb = (b * alpha + b_bg * (255 - alpha)) / 255;
                argb_buf[ty * w + tx] = 0xFF000000 | (fr << 16) | (fg_v << 8) | fb;
            }
        }
    }
    SDL_FreeSurface(fmt_surf);

    if (fxtk_is_offing() && argb_buf) {   /* 离屏画布(3D): 像素路径 */
        for (int ty = 0; ty < h; ty++)
            for (int tx = 0; tx < w; tx++) {
                uint32_t p = argb_buf[ty*w+tx];
                uint8_t fr=(p>>16)&0xFF, fg_v=(p>>8)&0xFF, fb=p&0xFF;
                fxtk_put_px(x+tx, y+ty, (uint32_t)((fr<<16)|(fg_v<<8)|fb));
            }
        free(argb_buf);
        return;
    }
    SDL_Texture *tex = NULL;
    if (argb_buf) {
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (tex) {
            SDL_UpdateTexture(tex, NULL, argb_buf, w * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        }
        free(argb_buf);
    }
    if (tex) fxtk_text_blit(tex, x, y, w, h);

    tc_entry_t *slot = tc_evict();
    if (slot) {
        char key[256];
        snprintf(key, sizeof(key), "%s|%06x|%06x", s, (unsigned)fg, (unsigned)bg);
        slot->font = g_font;
        slot->key = strdup(key);
        slot->w = w; slot->h = h;
        slot->tex = tex;
        slot->age = ++s_tc_clock;
    } else {
        if (tex) SDL_DestroyTexture(tex);
    }
}

void fx_draw_text(int x, int y, const char *s)
{
    fx_draw_text_c(x, y, s, FX_WHITE, FX_BLACK);
}

int fx_text_width_n(const char *s, int n)
{
    if (!s || n <= 0) return 0;
    char *t = (char *)malloc((size_t)n + 1);
    memcpy(t, s, (size_t)n); t[n] = 0;
    int w = fx_text_width(t);
    free(t);
    return w;
}
void fx_draw_text_c_n(int x, int y, const char *s, int n, fx_color_t fg, fx_color_t bg)
{
    if (!s || n <= 0) return;
    char *t = (char *)malloc((size_t)n + 1);
    memcpy(t, s, (size_t)n); t[n] = 0;
    fx_draw_text_c(x, y, t, fg, bg);
    free(t);
}

#define FC_N 8
static struct { int size; TTF_Font *f; } s_fc[FC_N];
static void tc_drop_font(TTF_Font *ft)
{ for(int i=0;i<TEXT_CACHE_SIZE;i++) if(s_tc[i].font==ft){ if(s_tc[i].tex)SDL_DestroyTexture(s_tc[i].tex); if(s_tc[i].key)free(s_tc[i].key); s_tc[i].tex=NULL;s_tc[i].key=NULL;s_tc[i].font=NULL;s_tc[i].age=0; } }
void fxtk_font_set_size(int size)
{
    TTF_Font *f = (TTF_Font *)fxtk_font_size(size);
    if (f) g_font = f;
}
void *fxtk_font_size(int size)
{
    if (size <= 0 || !s_font_path[0]) return g_font;
    for (int k = 0; k < FC_N; k++)
        if (s_fc[k].f && s_fc[k].size == size) return s_fc[k].f;
    static int s_fail[8];
    for (int k=0;k<8;k++) if(s_fail[k]==size) return g_font;
    TTF_Font *f = TTF_OpenFont(s_font_path, size);
    if (!f) { for(int k=0;k<8;k++) if(!s_fail[k]){s_fail[k]=size;break;} fprintf(stderr, "[fontcache] size=%d OPEN FAIL: %s\n", size, TTF_GetError()); return g_font; }
    int slot=-1; for (int k=0;k<FC_N;k++) if(!s_fc[k].f){slot=k;break;}
    if(slot<0){ slot=0; tc_drop_font(s_fc[0].f); TTF_CloseFont(s_fc[0].f); s_fc[0].f=NULL; s_fc[0].size=0; }
    s_fc[slot].f=f; s_fc[slot].size=size;
    return f;
}
int fxtk_text_width_size(int size, const char *t)
{
    TTF_Font *f = (TTF_Font *)fxtk_font_size(size);
    int w, h;
    if (!f || !t) return 0;
    TTF_SizeUTF8(f, t, &w, &h);
    return w;
}
void fxtk_draw_text_size(int size, int x, int y, const char *t, fx_color_t fg, fx_color_t bg)
{
    TTF_Font *old = g_font;
    g_font = (TTF_Font *)fxtk_font_size(size);
    fx_draw_text_c(x, y, t, fg, bg);
    g_font = old;
}
