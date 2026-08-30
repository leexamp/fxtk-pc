/**
 * canvas/01_primitives — 画布基础图元入门
 *
 * ★ 核心概念: canvas 回调里用的是【本地坐标】(0,0)~(cw-1,ch-1), 与窗口无关。
 *   - fx_canvas_size(w,&cw,&ch)  取画布本地宽高
 *   - fx_canvas_clear(w,color)   一键清底 (替代每帧 fx_set_color + fx_fill_rect 样板)
 *   - 每个 fx_draw_* / fx_fill_* 都自动遵守裁剪与当前前景色 fx_set_color()
 *
 * ★ 重要: 让内容【跟随画布缩放】——把坐标/尺寸写成 cw/ch 的比例(见下方 X/Y 宏),
 *   否则窗口拖大时内容会挤在一角(不符合预期)。本示例把图元分成三行, 互不重叠。
 */
#include "fxtk.h"

/* 0~1 比例 -> 实际像素 (让所有内容随 cw/ch 缩放) */
#define X(f) ((int)(cw * (f)))
#define Y(f) ((int)(ch * (f)))
#define S(f) ((int)((cw < ch ? cw : ch) * (f)))   /* 以较短边为基准的"正方块"边长 */

static void on_canvas(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    if (cw < 8 || ch < 8) return;
    fx_canvas_clear(w, FX_RGB(250, 250, 250));

    int m = X(0.04f);            /* 外边距 */
    int cy1 = Y(0.16f), cy2 = Y(0.50f), cy3 = Y(0.84f);   /* 三行中心 */
    int bh = S(0.14f);           /* 该行形状高度 */

    /* 边框 */
    fx_set_color(FX_BTN_BLUE);
    fx_draw_hline(m, cw - m, m);
    fx_draw_vline(m, m, ch - m);

    /* 行 1 (矩形组, 三个并排) */
    int y1 = cy1 - bh / 2;
    fx_set_color(FX_OK_GREEN);
    fx_fill_rect(X(0.08f), y1, X(0.30f), y1 + bh);
    fx_fill_rect_round(X(0.36f), y1, X(0.62f), y1 + bh, S(0.04f));
    fx_set_color(FX_UI_FG);
    fx_draw_rect(X(0.68f), y1, X(0.92f), y1 + bh);

    /* 行 2 (圆/椭圆/弧) */
    int r2 = S(0.10f);
    int y2 = cy2;
    fx_set_color(FX_RED_ACCENT);
    fx_draw_circle(X(0.16f), y2, r2 / 2);
    fx_fill_circle(X(0.32f), y2, r2 / 2);
    fx_draw_ellipse(X(0.50f), y2, S(0.12f), S(0.06f));
    fx_set_color(FX_PURPLE);
    fx_draw_arc(X(0.78f), y2, S(0.11f), 0, 120);

    /* 行 3 (三角 + 多边形) */
    int th = S(0.10f);
    int y3 = cy3;
    int16_t tri[] = { X(0.10f), y3 - th/2, X(0.24f), y3 - th/2, X(0.17f), y3 + th/2 };
    fx_set_color(FX_UI_FG);
    fx_draw_triangle(tri[0], tri[1], tri[2], tri[3], tri[4], tri[5]);
    int16_t poly[] = { X(0.40f), y3 - th/2, X(0.60f), y3 - th/2,
                       X(0.66f), y3, X(0.60f), y3 + th/2, X(0.40f), y3 + th/2 };
    fx_fill_polygon(poly, 5);

    fx_draw_text_c(m, ch - m - 12, "画布基础图元 (随窗口缩放)", FX_BTN_BLUE, FX_RGB(250, 250, 250));
}

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,262"), name("cv"), anim(1), call(on_canvas));
}
