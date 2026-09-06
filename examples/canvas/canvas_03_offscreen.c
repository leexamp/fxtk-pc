/**
 * canvas/03_offscreen — 画一个随窗口缩放的棋盘格 (直接绘制, 缩放安全)
 *
 * ★ 直接绘制 vs 离屏缓冲:
 *   - 本节用【直接绘制】, 让内容随窗口缩放、任何尺寸都不空白/崩坏。
 *   - 离屏缓冲 (fx_canvas_set_buf(w,1)) 更适合【固定尺寸 / 复杂静态内容】
 *     (整帧画进一块内存缓冲再一次性 blit, 减少逐图元合成开销)。用它之前要
 *     知道: 离屏画布在窗口【放大】时受驱动器/合成路径限制可能出空白,
 *     所以别让"会缩放的整屏画布"开离屏——固定尺寸的画布/静态内容再用。
 *
 * ★ 内容要随画布缩放: 地砖尺寸/小球半径都取 cw/ch 的比例。
 */
#include "fxtk.h"

static int s_tick = 0;

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    if (cw < 4 || ch < 4) return;
    fx_canvas_clear(w, FX_RGB(45, 45, 52));

    /* 地砖大小随画布缩放, 任何窗口下棋盘格比例一致 */
    int cell = cw / 20; if (cell < 6) cell = 6;
    for (int y = 0; y < ch; y += cell)
        for (int x = 0; x < cw; x += cell)
            if (((x / cell) + (y / cell)) & 1) {
                fx_set_color(FX_RGB(62, 62, 70));
                fx_fill_rect(x, y, x + cell - 1, y + cell - 1);
            }

    /* 小球: 半径随画布缩放, 起点不在贴边 */
    int r = (cw < ch ? cw : ch) / 14; if (r < 10) r = 10;
    int span = cw - 4 * r;
    int bx = (span > 0) ? (2 * r + (s_tick * 3 + span / 4) % span) : cw / 2;
    fx_set_color(FX_RED_ACCENT);
    fx_fill_circle(bx, ch / 2, r);

    fx_set_color(FX_LGRAY);
    fx_draw_text_c(8, 8, "离屏缓冲: 整帧画入缓冲后一次性 blit", FX_LGRAY, FX_RGB(45,45,52));
    s_tick++;
}

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,262"), name("cv"),
                  color(FX_RGB(45,45,52)), anim(1), call(on_cv));
    /* 直接绘制(不开离屏): 让棋盘格随窗口任意缩放都不空白 */
}
