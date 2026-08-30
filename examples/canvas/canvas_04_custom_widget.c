/**
 * canvas/04_custom_widget — 用画布做一个可复用的"自定义控件": 仪表盘
 *
 * ★ 思路: 标准控件不够用时, 用一个 canvas + 回调就能自绘任意控件。
 *   - 把画逻辑抽成函数 draw_gauge(cw,ch,value), 供多个画布复用
 *   - 值可以来自别的控件 (本示例: 滑条 value → 驱动仪表指针)
 *
 * 本示例: 一个滑条控制两个仪表盘 (一个圆表 + 一个环形进度), 演示自定义控件复用。
 */
#include "fxtk.h"
#include <math.h>
#include <stdio.h>

static int s_val = 70;

/* 画一个圆表: 中心 (cx,cy), 半径 r, 值 0~100 (240° 扫描, 从 -120° 开始) */
static void draw_gauge(int cx, int cy, int r, int value) {
    fx_set_color(FX_LGRAY);
    fx_draw_circle(cx, cy, r);                       /* 表盘 */

    /* 刻度: 每 10 一格 */
    fx_set_color(FX_UI_FG);
    for (int i = 0; i <= 10; i++) {
        double a = (-120.0 + i * 24.0) * 3.14159265 / 180.0;
        int tx = cx + (int)((r - 10) * sin(a));
        int ty = cy - (int)((r - 10) * cos(a));
        int tx2 = cx + (int)((r - 2) * sin(a));
        int ty2 = cy - (int)((r - 2) * cos(a));
        fx_draw_line(tx, ty, tx2, ty2);
    }

    /* 指针: 值越大越往右 */
    double na = (-120.0 + value * 2.4) * 3.14159265 / 180.0;
    int nx = cx + (int)((r - 14) * sin(na));
    int ny = cy - (int)((r - 14) * cos(na));
    fx_set_color(FX_RED_ACCENT);
    fx_draw_line(cx, cy, nx, ny);
    fx_fill_circle(cx, cy, 4);                       /* 中心轴 */
}

/* 画一个环形进度条: 中心 (cx,cy), 半径 r, 值 0~100 */
static void draw_ring(int cx, int cy, int r, int value) {
    fx_set_color(FX_LGRAY);
    for (int d = -3; d <= 3; d++) fx_draw_circle(cx, cy, r + d);   /* 粗圆环底 */
    fx_set_color(FX_OK_GREEN);
    double a0 = -90.0;                               /* 从顶点开始 */
    for (int d = -3; d <= 3; d++) {
        /* 用多段小弧拼出绿色进度环 */
        for (int i = 0; i < value; i += 2) {
            double a = (a0 + i * 3.6) * 3.14159265 / 180.0;
            int tx = cx + (int)((r + d) * cos(a));
            int ty = cy + (int)((r + d) * sin(a));
            fx_draw_pixel(tx, ty);
        }
    }
}

static void on_gauge(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(250, 250, 250));

    /* 半径随画布缩放, 但要"能摆下两个表": 受高度与"每个表占 ~1/4 宽"双重限制, 任何宽高比都均衡不溢出 */
    int rh = (int)(ch * 0.32f);            /* 高度允许的半径 */
    int rw = (int)(cw * 0.125f);           /* 宽度允许的半径 (两个表并排) */
    int r = rh < rw ? rh : rw; if (r < 12) r = 12;
    int cy = ch / 2 - 8;

    /* 左侧圆表 */
    draw_gauge(cw / 4, cy, r, s_val);

    /* 右侧环形进度 (半径略小 0.7 倍) */
    draw_ring(cw * 3 / 4, cy, (int)(r * 0.7f), s_val);

    char b[48];
    snprintf(b, sizeof b, "SDF 值: %d  (拖滑条)", s_val);
    fx_draw_text_c(12, ch - 26, b, FX_UI_FG, FX_RGB(250, 250, 250));
}

static void on_slider(fx_widget_t *w, void *ud) {
    (void)ud;
    s_val = fx_get_value(w);
    fx_repaint();
}

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,230"), name("gauge"),
                  color(FX_RGB(250,250,250)), anim(1), call(on_gauge));
    fx_slider_new(pixel("10,244", "470,264"), value(s_val), color(FX_OK_GREEN), call(on_slider));
}
