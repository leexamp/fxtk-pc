/**
 * render_canvas.c — 把某个 canvas 示例的 app_init() 真实渲染成一帧, 导出 PPM
 *
 * 用途: 在无 SDL/窗口的环境下, 用【假驱动 + 纯软件像素路径】把示例渲染到内存,
 * 再导出 PPM, 供开发者(或 AI)查看不同窗口尺寸下的真实绘制结果, 验证 resize 行为。
 *
 * 用法:
 *   render_canvas <W> <H> <out.ppm>   # 与某个示例的 app_init 一起链接
 *
 * 关键: 把驱动里的 fill_rect/draw_line/fill_tri/blit 等置空, 强制所有图元走
 * fxtk_put_px -> set_window/push_pixels 的软件路径, 从而被假驱动完整捕获。
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void fxtk_draw_all(void);   /* 由 fxtk_internal.h 声明, 这里单独引出 */

/* ---------- 文字/纹理桩 (无 SDL/字体) ---------- */
void fx_draw_text(int x, int y, const char *s) {(void)x;(void)y;(void)s;}
void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg) {(void)x;(void)y;(void)s;(void)fg;(void)bg;}
void fx_draw_text_c_n(int x, int y, const char *s, int n, fx_color_t fg, fx_color_t bg) {(void)x;(void)y;(void)s;(void)n;(void)fg;(void)bg;}
int  fx_text_width(const char *s) {(void)s; return 0;}
int  fx_text_width_n(const char *s, int n) {(void)s;(void)n; return 0;}
void fxtk_draw_text_size(int size, int x, int y, const char *t, fx_color_t fg, fx_color_t bg) {(void)size;(void)x;(void)y;(void)t;(void)fg;(void)bg;}
int  fxtk_text_width_size(int size, const char *t) {(void)size;(void)t; return 0;}
void *fxtk_font_size(int size) {(void)size; return NULL;}
int  fxtk_font_height(int size) {(void)size; return 0;}
void fxtk_font_set_size(int size) {(void)size;}

/* ---------- SDL 驱动才有的符号桩 ---------- */
int  fxtk_fps(void) { return 60; }
int  fxtk_shift_down(void) { return 0; }
int  fxtk_right_click(int *x, int *y) {(void)x;(void)y; return 0;}
int  fxtk_ui_scale(void) { return 100; }

/* ---------- 假驱动: 捕获像素到 framebuffer ---------- */
static uint32_t *s_fb = NULL; static int s_fbw = 0, s_fbh = 0;
static int gx0, gy0, gx1, gy1;

static int drv_init(void) { return 0; }
static void drv_set_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) { gx0=x0; gy0=y0; gx1=x1; gy1=y1; }
static void drv_push_pixels(const uint32_t *px, uint32_t n) {
    int w = gx1 - gx0 + 1; if (w <= 0) w = 1;
    for (uint32_t i = 0; i < n; i++) {
        int x = gx0 + (int)(i % (uint32_t)w);
        int y = gy0 + (int)(i / (uint32_t)w);
        if (x >= 0 && x < s_fbw && y >= 0 && y < s_fbh) s_fb[y * s_fbw + x] = px[i];
    }
}
static void drv_hold_begin(void) {}
static void drv_hold_end(void) {}
/* 置空 fill_rect/draw_line/fill_tri/blit -> 强制走软件像素路径, 才能被捕获 */
static int  drv_touch_read(int *x, int *y, int *pressed) {(void)x;(void)y;(void)pressed; return 0;}
static int  drv_key_read(fx_keyev_t *ev) {(void)ev; return 0;}
static void drv_clip_set(const char *s) {(void)s;}
static const char *drv_clip_get(void) { return ""; }
static void drv_set_title(const char *s) {(void)s;}
static int  drv_wheel_read(int *x, int *y, int *dy) {(void)x;(void)y;(void)dy; return 0;}
static void drv_set_clip_rect(int x1,int y1,int x2,int y2) {(void)x1;(void)y1;(void)x2;(void)y2;}

/* 简易最近邻图像 blit, 便于在软件路径下也看到图片 (验证明缩放) */
static void drv_blit_img(const uint32_t *px,int w,int h,int dx,int dy,int dw,int dh,int dark) {
    (void)dark;
    if (!px || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    for (int ty = 0; ty < dh; ty++) {
        int sy = ty * h / dh;
        for (int tx = 0; tx < dw; tx++) {
            int sx = tx * w / dw;
            int x = dx + tx, y = dy + ty;
            if (x >= 0 && x < s_fbw && y >= 0 && y < s_fbh) s_fb[y * s_fbw + x] = px[sy * w + sx];
        }
    }
}

static fx_driver_t s_drv = {
    .width = 480, .height = 272,
    .init = drv_init,
    .set_window = drv_set_window,
    .push_pixels = drv_push_pixels,
    .hold_begin = drv_hold_begin,
    .hold_end = drv_hold_end,
    .touch_read = drv_touch_read,
    .key_read = drv_key_read,
    .clip_set = drv_clip_set,
    .clip_get = drv_clip_get,
    .set_title = drv_set_title,
    .wheel_read = drv_wheel_read,
    .set_clip_rect = drv_set_clip_rect,
    .blit_img = drv_blit_img,
    /* 其余置空 -> 走软件像素路径 (fill_rect/draw_line/fill_tri/blit_tex/blit_img_rot) */
    .fill_rect = NULL, .draw_line = NULL, .fill_tri = NULL,
    .blit_img_rot = NULL, .blit_tex = NULL,
};

extern void app_init(void);

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 480;
    int H = argc > 2 ? atoi(argv[2]) : 272;
    const char *out = argc > 3 ? argv[3] : "/tmp/fxtk_frame.ppm";
    if (W <= 0 || H <= 0) { fprintf(stderr, "bad size\n"); return 1; }

    s_drv.width = (uint16_t)W; s_drv.height = (uint16_t)H;
    s_fb = (uint32_t *)malloc((size_t)W * H * 4);
    if (!s_fb) return 1;
    s_fbw = W; s_fbh = H;

    fx_init(&s_drv);
    app_init();
    fx_layout();
    fxtk_draw_all();

    /* 写 PPM (P6) */
    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint32_t c = s_fb[i];
        unsigned char r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        fwrite(&r, 1, 1, f); fwrite(&g, 1, 1, f); fwrite(&b, 1, 1, f);
    }
    fclose(f);
    free(s_fb);
    fprintf(stderr, "wrote %s (%dx%d)\n", out, W, H);
    return 0;
}
