/**
 * headless_test.c — 无窗口单元测试
 *
 * 用假驱动的 fx_driver_t 直接驱动核心库, 不初始化 SDL/窗口/字体:
 * 只验证布局(pixel/percent/grid)、命中测试(press/release)、控件生命周期,
 * 以及 value 读写。文字/纹理在无字体时为 no-op, 不影响布局结果。
 *
 * 构建(见 Makefile 的 test 目标):
 *   gcc -I.. -I../components/fxtk \
 *       ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
 *       ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_extra.c \
 *       ../components/fxtk/fxtk_effects.c \
 *       headless_test.c -o headless_test -lm
 *   ./headless_test
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---------- 假驱动 ---------- */
static int s_hit_cb_ok = 0;          /* 命中回调用触发器 */

static int drv_init(void) { return 0; }
static void drv_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {(void)x0;(void)y0;(void)x1;(void)y1;}
static void drv_push_pixels(const uint32_t *px, uint32_t n) {(void)px;(void)n;}
static void drv_hold_begin(void) {}
static void drv_hold_end(void) {}
static void drv_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t c) {(void)x0;(void)y0;(void)x1;(void)y1;(void)c;}
static int  drv_touch_read(int *x, int *y, int *pressed) {(void)x;(void)y;(void)pressed; return 0;}
static int  drv_key_read(fx_keyev_t *ev) {(void)ev; return 0;}
static void drv_clip_set(const char *s) {(void)s;}
static const char *drv_clip_get(void) { return ""; }
static void drv_set_title(const char *s) {(void)s;}
static int  drv_wheel_read(int *x, int *y, int *dy) {(void)x;(void)y;(void)dy; return 0;}
static void drv_blit_img(const uint32_t *px,int w,int h,int dx,int dy,int dw,int dh,int dark) {(void)px;(void)w;(void)h;(void)dx;(void)dy;(void)dw;(void)dh;(void)dark;}
static void drv_set_clip_rect(int x1,int y1,int x2,int y2) {(void)x1;(void)y1;(void)x2;(void)y2;}
static void drv_fill_tri(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t c) {(void)x1;(void)y1;(void)x2;(void)y2;(void)x3;(void)y3;(void)c;}
static void drv_draw_line(int x1,int y1,int x2,int y2,uint32_t c) {(void)x1;(void)y1;(void)x2;(void)y2;(void)c;}
static void drv_blit_tex(void *t,int sx,int sy,int sw,int sh,int dx,int dy) {(void)t;(void)sx;(void)sy;(void)sw;(void)sh;(void)dx;(void)dy;}
static void drv_blit_img_rot(const uint32_t *px,int w,int h,int cx,int cy,int dw,int dh,double ang) {(void)px;(void)w;(void)h;(void)cx;(void)cy;(void)dw;(void)dh;(void)ang;}

static fx_driver_t s_drv = {
    .width = 480, .height = 272,
    .init = drv_init,
    .set_window = drv_set_window,
    .push_pixels = drv_push_pixels,
    .hold_begin = drv_hold_begin,
    .hold_end = drv_hold_end,
    .fill_rect = drv_fill_rect,
    .touch_read = drv_touch_read,
    .key_read = drv_key_read,
    .clip_set = drv_clip_set,
    .clip_get = drv_clip_get,
    .set_title = drv_set_title,
    .wheel_read = drv_wheel_read,
    .blit_img = drv_blit_img,
    .set_clip_rect = drv_set_clip_rect,
    .fill_tri = drv_fill_tri,
    .draw_line = drv_draw_line,
    .blit_tex = drv_blit_tex,
    .blit_img_rot = drv_blit_img_rot,
};

/* ---------- 文字/纹理桩 (无 SDL/字体, 均为 no-op) ---------- */
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
int  fxtk_ui_scale(void) { return 100; }   /* 480 设计宽=100 */

/* ---------- 测试回调 ---------- */
static void on_hit(fx_widget_t *w, void *ud) { (void)w; (void)ud; s_hit_cb_ok = 1; }

static void check_rect(const char *what, fx_widget_t *w, int x1,int y1,int x2,int y2) {
    int ax1=-1,ay1=-1,ax2=-1,ay2=-1;
    fx_widget_rect(w,&ax1,&ay1,&ax2,&ay2);
    if (ax1!=x1||ay1!=y1||ax2!=x2||ay2!=y2) {
        printf("  [FAIL] %s: got (%d,%d,%d,%d) want (%d,%d,%d,%d)\n",
               what, ax1,ay1,ax2,ay2, x1,y1,x2,y2);
        assert(0);
    }
    printf("  [ok]   %s = (%d,%d,%d,%d)\n", what, ax1,ay1,ax2,ay2);
}

int main(void) {
    int fails = 0;
    fx_init(&s_drv);

    printf("== fxtk headless test ==\n");

    /* 1. pixel 布局 */
    printf("[1] pixel 绝对定位\n");
    fx_button_new(pixel("10,10","100,40"), name("b1"), title("A"));
    fx_layout();
    check_rect("b1 pixel", fx_find("b1"), 10,10,100,40);

    /* 2. percent 布局 (480x272): 25%~75% 应得 (120,68,359,203) */
    printf("[2] percent 响应式\n");
    fx_label_new(percent("0.25,0.25","0.75,0.75"), name("l1"), title("B"));
    fx_layout();
    check_rect("l1 percent", fx_find("l1"), 120,68,359,203);

    /* 3. grid 布局: 3x3 网格 (0,0)-(300,180), 单元格 100x60 */
    printf("[3] grid 网格\n");
    fx_grid_map(pixel("0,0","300,180"), line(3), row(3), name("g"));
    fx_button_new(grid("g",2,2,2,2), name("gbtn"), title("X"));
    fx_layout();
    /* 单元(2,2): x1=0+(2-1)*100=100, y1=0+(2-1)*60=60, x2=0+2*100-1=199, y2=0+2*60-1=119 */
    check_rect("gbtn grid", fx_find("gbtn"), 100,60,199,119);

    /* 4. 命中测试: 点击按钮内应触发回调, 点击外面不应 */
    printf("[4] 命中测试 press/release\n");
    fx_button_new(pixel("200,200","280,240"), name("hit"), title("Hit"), call(on_hit));
    fx_layout();
    s_hit_cb_ok = 0;
    fx_touch_press(220,220);   /* 命中 */
    fx_touch_release(220,220);
    if (!s_hit_cb_ok) { printf("  [FAIL] 命中区域内未触发回调\n"); fails++; }
    else printf("  [ok]   命中回调触发\n");
    s_hit_cb_ok = 0;
    fx_touch_press(10,10);     /* 未命中 */
    fx_touch_release(10,10);
    if (s_hit_cb_ok) { printf("  [FAIL] 命中区域外却触发了回调\n"); fails++; }
    else printf("  [ok]   区域外未触发\n");

    /* 5. value 读写 */
    printf("[5] value 读写\n");
    fx_slider_new(pixel("10,100","200,116"), name("s1"));
    fx_layout();
    fx_set_value(fx_find("s1"), 42);
    if (fx_get_value(fx_find("s1")) != 42) { printf("  [FAIL] set/get value\n"); fails++; }
    else printf("  [ok]   value=42\n");

    /* 6. 控件计数 */
    printf("[6] 控件计数\n");
    int n0 = fxtk_widget_count();
    fx_button_new(pixel("10,250","50,270"), name("extra"), title("E"));
    fx_layout();
    int n1 = fxtk_widget_count();
    if (n1 != n0 + 1) { printf("  [FAIL] 控件计数 %d -> %d\n", n0, n1); fails++; }
    else printf("  [ok]   count %d -> %d\n", n0, n1);

    /* 7. 查找与删除 (fx_delete 用 fx_wptr 或 grid 定位, 不是 name) */
    printf("[7] fx_find / fx_delete\n");
    if (!fx_find("extra")) { printf("  [FAIL] fx_find 未找到 extra\n"); fails++; }
    else {
        fx_widget_t *e = fx_find("extra");
        fx_delete(fx_wptr(e));
        if (fx_find("extra")) { printf("  [FAIL] fx_delete 后仍能找到\n"); fails++; }
        else printf("  [ok]   find+delete 正常\n");
    }

    /* 8. 画布便捷 API: fx_canvas_size + fx_canvas_clear (v2.2 新增) */
    printf("[8] 画布便捷 API\n");
    fx_canvas_new(pixel("10,10","100,50"), name("cvx"));
    fx_layout();
    int cw = 0, ch = 0;
    fx_canvas_size(fx_find("cvx"), &cw, &ch);
    if (cw != 91 || ch != 41) { printf("  [FAIL] canvas size=%dx%d want 91x41\n", cw, ch); fails++; }
    else printf("  [ok]   canvas size %dx%d\n", cw, ch);
    fx_canvas_clear(fx_find("cvx"), FX_WINDOW_BG);   /* 仅要求不崩溃 */
    printf("  [ok]   canvas_clear 无异常\n");

    printf("== done: %s (%d fail) ==\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
