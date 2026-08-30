/**
 * canvas/07_aa — 抗锯齿（平滑渲染）
 *
 * ★ 用法: 调 `fx_set_aa(1)` 后, 画布会【自动离屏】以支持边缘混合; 之后的
 *   line/circle/fill_circle/rect/round-rect/arc 都按"到图形的距离"做边缘覆盖,
 *   边缘平滑无锯齿。`fx_set_aa(0)` 关闭。
 *
 *   - 按钮用来实时切换 抗锯齿开/关, 对比锯齿与平滑两种边缘。
 *   - 注: 抗锯齿/离屏画布在窗口放大时受驱动/合成路径限制可能空白(见 canvas_03 说明)。
 */
#include "fxtk.h"

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud; int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(250, 250, 250));
    int m = cw / 20, cy = ch / 2;

    fx_set_color(FX_UI_FG);
    fx_draw_circle(cw / 2, cy, 62);                 /* 圆 */
    fx_draw_arc(cw - 90, cy, 58, 0, 130);           /* 圆弧 */
    fx_draw_rect(m, cy - 60, m + 140, cy + 30);     /* 空心矩形 */
    fx_draw_line(m, m, m + 120, m + 90);            /* 斜线 */

    fx_set_color(FX_OK_GREEN);
    fx_fill_rect_round(m, ch - 60, cw / 2 - 40, ch - 20, 16);   /* 圆角实心矩形 */

    fx_set_color(FX_RED_ACCENT);
    fx_fill_circle(cw / 2, cy, 5);                  /* 中心红点 */

    fx_draw_text_c(m, ch - m - 12, fxtk_aa() ? "抗锯齿: ON" : "抗锯齿: OFF",
                   FX_BTN_BLUE, FX_RGB(250, 250, 250));
}

static void on_toggle(fx_widget_t *w, void *ud) { (void)w; (void)ud; fx_set_aa(!fxtk_aa()); fx_repaint(); }

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_set_aa(1);                                    /* 开抗锯齿: 画布自动离屏以支持混合 */
    fx_canvas_new(pixel("10,10", "470,232"), name("aa"), anim(1), call(on_cv));
    fx_button_new(pixel("10,240", "150,266"), title("切换抗锯齿"), color(FX_BTN_BLUE), call(on_toggle));
}
