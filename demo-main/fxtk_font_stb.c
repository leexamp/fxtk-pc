/**
 * fxtk_font_stb.c — v2.4 sokol 路径的文本层 (stb_truetype + sokol_gfx)
 *
 * 与 fxtk_font.c (SDL_ttf 版) 实现同一套文本 API, 但:
 *  - 字体解析/光栅化用 vendored stb_truetype, 不依赖 SDL_ttf / SDL_Renderer
 *  - 纹理用 sokol_gfx 创建 (fxtk_sokol_tex_t), 通过驱动的 blit_tex 绘制
 *  - 纹理按 (字号, 字符串, 前景色, 背景色) 缓存, LRU 淘汰; 同时保留 CPU 侧像素,
 *    以便离屏画布(抗锯齿/自绘)走像素路径 —— 与 SDL 版行为一致
 *  - 宽度查询按字形 advance 累加 (缓存), 为逐字符排版(textedit 光标/换行)服务
 *
 * 已知取舍: 每串一张纹理(与 SDL 版相同)。字形图集 + 批量绘制是 P2.3 的任务。
 */
#define _POSIX_C_SOURCE 200809L
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb/stb_truetype.h"

#include "fxtk_sokol.h"
#include "fxtk_backends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 框架钩子 (fxtk_draw.c 提供) */
extern int  fxtk_is_offing(void);
extern void fxtk_put_px(int x, int y, uint32_t c);
extern void fxtk_text_blit(void *tex, int x, int y, int w, int h);

#define FONT_MAX  8
#define TC_MAX    128

typedef struct {
    int size;
    int ok;
    stbtt_fontinfo info;
    unsigned char *data;      /* TTF 文件内容 (常驻) */
    float scale;
    int ascent, descent, linegap;
} font_t;

static font_t s_fonts[FONT_MAX];
static int    s_font_n = 0;
static int    s_cur_size = 16;
static char   s_path[512];

typedef struct {
    font_t *f;
    char   *key;
    uint32_t age;
    int w, h;
    fxtk_sokol_tex_t tex;
    unsigned char *px;        /* RGBA, 离屏路径用 */
} tc_t;
static tc_t   s_tc[TC_MAX];
static uint32_t s_clock = 0;
int fxtk_text_created = 0;      /* 诊断: 已创建纹理数(缓存未命中) */
int fxtk_text_evicted = 0;

/* ================= 字体加载 ================= */

static const char *default_font_paths[] = {
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/msyh.ttc",
    "C:/Windows/Fonts/simhei.ttf",
    "C:/Windows/Fonts/arial.ttf",
    NULL
};

void fxtk_font_init(const char *unused_path, int size)
{
    (void)unused_path;
    if (s_path[0]) return;
    const char *env = getenv("FXTK_FONT");
    const char *try_list[16];
    int n = 0;
    if (env && env[0]) try_list[n++] = env;
    for (int i = 0; default_font_paths[i] && n < 15; i++) try_list[n++] = default_font_paths[i];
    try_list[n] = NULL;

    for (int i = 0; i < n; i++) {
        int sz = 0;
        void *data = fx_file_read(try_list[i], &sz);
        if (!data || sz <= 0) { fx_file_free(data); continue; }
        snprintf(s_path, sizeof(s_path), "%s", try_list[i]);
        /* 首个字号在此加载, 后续字号复用同一份数据 */
        font_t *f = &s_fonts[0];
        f->data = (unsigned char *)data;
        f->ok = stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0));
        f->size = size;
        f->scale = f->ok ? stbtt_ScaleForPixelHeight(&f->info, (float)size) : 0.0f;
        if (f->ok) {
            stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->linegap);
            s_font_n = 1;
            s_cur_size = size;
            fx_log(FX_LOG_INFO, "[font-stb] %s (size=%d, ascent=%d)", s_path, size, f->ascent);
            return;
        }
        fx_file_free(data);
        s_path[0] = 0;
    }
    fx_log(FX_LOG_ERROR, "[font-stb] 无法加载任何字体 (可用 FXTK_FONT 指定)");
}

static font_t *font_get(int size)
{
    if (size <= 0) size = s_cur_size;
    for (int i = 0; i < s_font_n; i++)
        if (s_fonts[i].size == size) return &s_fonts[i];
    if (!s_path[0]) return s_font_n ? &s_fonts[0] : NULL;
    int slot = s_font_n;
    if (slot >= FONT_MAX) {          /* 满了: 淘汰最久未用的字号(1 号起, 0 号留作默认),
                                      * 旧实现直接退回 16px → 演示里"字号滑杆只能小范围缩放" */
        slot = 1;
        for (int i = 2; i < FONT_MAX; i++)
            if (s_fonts[i].size != s_cur_size && s_fonts[i].size < s_fonts[slot].size) slot = i;
        free(s_fonts[slot].data);
        memset(&s_fonts[slot], 0, sizeof(font_t));
    } else {
        s_font_n++;
    }
    font_t *f = &s_fonts[slot];
    memset(f, 0, sizeof(*f));
    f->data = (unsigned char *)fx_file_read(s_path, &(int){0});
    if (!f->data) return &s_fonts[0];
    f->ok = stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0));
    if (!f->ok) { fx_file_free(f->data); return &s_fonts[0]; }
    f->size = size;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->linegap);
    return f;
}

void  fxtk_font_set_size(int size) { if (size > 0) s_cur_size = size; }
void *fxtk_font_size(int size) { return (void *)font_get(size); }

/* ================= 文本度量 ================= */

static uint32_t utf8_next(const char *s, int *adv)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = p[0];
    if (cp < 0x80) { *adv = 1; return cp; }
    if ((cp & 0xE0) == 0xC0) { cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F); *adv = 2; return cp; }
    if ((cp & 0xF0) == 0xE0) { cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); *adv = 3; return cp; }
    cp = ((cp & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); *adv = 4; return cp;
}

static int measure(font_t *f, const char *s, int n)
{
    if (!f || !f->ok || !s) return 0;
    float w = 0.0f;
    int i = 0;
    while (s[i] && (n < 0 || i < n)) {
        int adv; uint32_t cp = utf8_next(s + i, &adv);
        int a, b;
        stbtt_GetCodepointHMetrics(&f->info, (int)cp, &a, &b);
        w += (float)a * f->scale;
        i += adv;
    }
    return (int)(w + 0.5f);
}

int fx_text_width(const char *s) { return measure(font_get(s_cur_size), s, -1); }

int fx_text_width_n(const char *s, int n)
{
    if (!s || n <= 0) return 0;
    return measure(font_get(s_cur_size), s, n);
}

int fxtk_text_width_size(int size, const char *t)
{
    if (!t) return 0;
    return measure(font_get(size), t, -1);
}

/* ================= 纹理缓存 ================= */

static void tc_free(tc_t *e)
{
    if (e->tex.view.id != SG_INVALID_ID) sg_destroy_view(e->tex.view);
    if (e->tex.img.id != SG_INVALID_ID) sg_destroy_image(e->tex.img);
    free(e->key); free(e->px);
    memset(e, 0, sizeof(*e));
}

/* 把字符串光栅化成 RGBA (前景色叠在背景色上, 与 SDL 版一致) */
static unsigned char *rasterize(font_t *f, const char *s, fx_color_t fg, fx_color_t bg,
                               int *out_w, int *out_h)
{
    int w = measure(f, s, -1) + 2;
    int h = (int)((f->ascent - f->descent) * f->scale + 0.5f) + 2;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return NULL;
    unsigned char br = (unsigned char)((bg >> 16) & 0xFF), bgc = (unsigned char)((bg >> 8) & 0xFF), bb = (unsigned char)(bg & 0xFF);
    unsigned char fr = (unsigned char)((fg >> 16) & 0xFF), fgc = (unsigned char)((fg >> 8) & 0xFF), fb = (unsigned char)(fg & 0xFF);
    /* 字节序(2026-09 修正): sokol 的 SG_PIXELFORMAT_RGBA8 是标准 RGBA 布局 —— 内存第 0 字节 = R。
     * 以前这里按 B,G,R 写(说是"实测"), 那是被回读路径的双重互换骗了: 结果彩色文字/彩色底反色
     * (蓝字变橙字), 而且与顶点色互相矛盾。纹理与顶点现在统一成【byte0 = R】。 */
    for (int i = 0; i < w * h; i++) { px[i*4+0] = br; px[i*4+1] = bgc; px[i*4+2] = bb; px[i*4+3] = 255; }

    int baseline = (int)(f->ascent * f->scale + 0.5f) + 1;
    float x = 1.0f;
    int i = 0;
    while (s[i]) {
        int adv; uint32_t cp = utf8_next(s + i, &adv);
        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char *bmp = stbtt_GetCodepointBitmap(&f->info, f->scale, f->scale, (int)cp, &gw, &gh, &gx, &gy);
        if (bmp) {
            int ox = (int)x + gx, oy = baseline + gy;
            for (int yy = 0; yy < gh; yy++) {
                int py = oy + yy;
                if (py < 0 || py >= h) continue;
                for (int xx = 0; xx < gw; xx++) {
                    int pxx = ox + xx;
                    if (pxx < 0 || pxx >= w) continue;
                    unsigned int a = bmp[yy * gw + xx];
                    if (!a) continue;
                    unsigned char *d = &px[((size_t)py * w + pxx) * 4];
                    d[0] = (unsigned char)((fr  * a + d[0] * (255 - a)) / 255);   /* R */
                    d[1] = (unsigned char)((fgc * a + d[1] * (255 - a)) / 255);   /* G */
                    d[2] = (unsigned char)((fb  * a + d[2] * (255 - a)) / 255);   /* B */
                }
            }
            stbtt_FreeBitmap(bmp, NULL);
        }
        int a1, b1;
        stbtt_GetCodepointHMetrics(&f->info, (int)cp, &a1, &b1);
        x += (float)a1 * f->scale;
        i += adv;
    }
    *out_w = w; *out_h = h;
    return px;
}

static tc_t *tc_find(font_t *f, const char *s, fx_color_t fg, fx_color_t bg)
{
    for (int i = 0; i < TC_MAX; i++)
        if (s_tc[i].key && s_tc[i].f == f && strcmp(s_tc[i].key, s) == 0 &&
            s_tc[i].w == 0 /* 占位 */ ) { }
    /* 键里必须含颜色: 用组合键比较 */
    char key[256];
    snprintf(key, sizeof(key), "%d|%06x|%06x|%s", f ? f->size : 0, (unsigned)fg, (unsigned)bg, s);
    for (int i = 0; i < TC_MAX; i++)
        if (s_tc[i].key && strcmp(s_tc[i].key, key) == 0) { s_tc[i].age = ++s_clock; return &s_tc[i]; }
    return NULL;
}

static tc_t *tc_alloc(void)
{
    for (int i = 0; i < TC_MAX; i++) if (!s_tc[i].key) return &s_tc[i];
    int oldest = 0;
    for (int i = 1; i < TC_MAX; i++) if (s_tc[i].age < s_tc[oldest].age) oldest = i;
    fxtk_text_evicted++;
    tc_free(&s_tc[oldest]);
    return &s_tc[oldest];
}

/* ================= 绘制 ================= */

static void draw_pixels_offscreen(const unsigned char *px, int w, int h, int x, int y)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const unsigned char *p = &px[((size_t)j * w + i) * 4];
            fxtk_put_px(x + i, y + j, ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
        }
}

void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg)
{
    if (!s || !s[0]) return;
    font_t *f = font_get(s_cur_size);
    if (!f || !f->ok) return;

    tc_t *e = tc_find(f, s, fg, bg);
    if (!e) {
        int w = 0, h = 0;
        unsigned char *px = rasterize(f, s, fg, bg, &w, &h);
        if (!px) return;
        e = tc_alloc();
        char key[256];
        snprintf(key, sizeof(key), "%d|%06x|%06x|%s", f->size, (unsigned)fg, (unsigned)bg, s);
        e->key = strdup(key);
        e->f = f; e->w = w; e->h = h; e->px = px; e->age = ++s_clock;
        fxtk_text_created++;
        e->tex.img = sg_make_image(&(sg_image_desc){
            .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8,
            .usage.dynamic_update = true, .label = "fxtk-text" });
        e->tex.view = sg_make_view(&(sg_view_desc){ .texture = { .image = e->tex.img }, .label = "fxtk-text-view" });
        e->tex.w = w; e->tex.h = h;
        sg_update_image(e->tex.img, &(sg_image_data){ .mip_levels[0] = { px, (size_t)w * h * 4 } });
    }
    if (fxtk_is_offing() || e->tex.img.id == SG_INVALID_ID) {   /* 离屏画布: 走像素路径 */
        draw_pixels_offscreen(e->px, e->w, e->h, x, y);
        return;
    }
    fxtk_text_blit(&e->tex, x, y, e->w, e->h);
}

void fx_draw_text(int x, int y, const char *s) { fx_draw_text_c(x, y, s, FX_WHITE, FX_BLACK); }

void fx_draw_text_c_n(int x, int y, const char *s, int n, fx_color_t fg, fx_color_t bg)
{
    if (!s || n <= 0) return;
    char sbuf[128];
    char *t = (n < (int)sizeof(sbuf) - 1) ? sbuf : (char *)malloc((size_t)n + 1);
    if (!t) return;
    memcpy(t, s, (size_t)n); t[n] = 0;
    fx_draw_text_c(x, y, t, fg, bg);
    if (t != sbuf) free(t);
}

void fxtk_draw_text_size(int size, int x, int y, const char *t, fx_color_t fg, fx_color_t bg)
{
    if (size <= 0) size = 16;
    if (size > 120) size = 120;
    int old = s_cur_size;
    s_cur_size = size;
    fx_draw_text_c(x, y, t, fg, bg);
    s_cur_size = old;
}
