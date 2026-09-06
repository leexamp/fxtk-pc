/** ex17_tabs — 立体标签页 + 多页布局 + page() 页闸 */
#include "fxtk.h"
#include <stdio.h>
#include <math.h>

static int s_n = 0;
static void on_btn(fx_widget_t *w, void *ud) {
    (void)ud;
    s_n++;
    char b[32]; snprintf(b, sizeof(b), "页1 被点 %d 次", s_n);
    fx_set_title(fx_find("cnt1"), b);
}
static void on_wave(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_RGB(30, 30, 30)); fx_fill_rect(0, 0, cw - 1, ch - 1);
    fx_set_color(FX_YELLOW);
    static int t = 0;
    for (int x = 0; x < cw; x += 2) {
        int y = ch / 2 + (int)(sinf((x + t) * 0.08f) * ch / 3);
        fx_draw_vline(x, y, y);
    }
    t++;
}
static void on_draw2(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_RGB(240, 240, 240)); fx_fill_rect(0, 0, cw - 1, ch - 1);
    fx_set_color(FX_RGB(33, 150, 243));
    fx_fill_rect(20, 20, cw - 20, 60);
    fx_set_color(FX_RGB(244, 67, 54));
    fx_fill_circle(cw / 2, ch / 2, ch / 4);
}
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_tab_new(pixel("10,26", "470,266"), title("页面一,页面二,页面三"),
               name("tab"), color(FX_RGB(224, 224, 224)));
    fx_parent(fx_find("tab"));

    /* 页0: 按钮 + 计数 */
    fx_button_new(pixel("30,50", "200,90"), page(0), title("点我"), call(on_btn));
    fx_label_new(pixel("30,100", "400,130"), page(0), name("cnt1"),
                 title("页1 被点 0 次"));

    /* 页1: 动画波形 */
    fx_canvas_new(pixel("20,40", "450,240"), page(1), anim(1),
                  color(FX_RGB(30, 30, 30)), call(on_wave));

    /* 页2: 静态矢量 */
    fx_canvas_new(pixel("20,40", "450,240"), page(2), call(on_draw2));

    fx_parent(NULL);
}
