/** ex05_scroll — 画布自绘长列表 + 滚动条 (核心滚动: fx_scroll_update) */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
static void on_view(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    int row = 36, total = 100 * row;
    int off = fx_scroll_update(w, total);   /* 核心滚动: rc 手感 (滚轮/插值/重绘 全在库内) */
    fx_set_color(FX_WHITE); fx_fill_rect(0, 0, cw - 1, ch - 1);
    for (int i = off / row; i < 100; i++) {
        int y = i * row - off; if (y > ch) break;
        if (i & 1) { fx_set_color(FX_RGB(225, 238, 252)); fx_fill_rect(0, y, cw - 1, y + row - 1); }
        char t[32]; snprintf(t, sizeof(t), "第 %d 项", i + 1);
        fx_draw_text_c(12, y + 9, t, FX_RGB(30, 30, 30), FX_WHITE);
    }
    fx_scrollbar_draw(w, off, total);   /* 库滚动条: 轨道+滑块 */
}
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_canvas_new(pixel("10,10", "470,262"), anim(1), color(FX_WHITE), call(on_view));
}
