/**
 * canvas/06_interact — 在画布里做交互 (触摸拖动)
 *
 * ★ 画布不仅是显示, 也能响应触摸。用桌面扩展 API:
 *   - fx_touch_state(&mx,&my,&mp)  取当前鼠标/触摸坐标与按下状态 (悬停也更新)
 *   - fx_pressed()                 返回当前被按下的控件 (== 该画布即可拖动)
 *   - 世界坐标 → 画布本地坐标: 减去画布左上角 (w->x1,w->y1)
 *
 * 本示例: 按住画布内任意处拖动一个小球。
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>

static int s_x = 200, s_y = 100;   /* 球的画布本地坐标 */

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    int r = (cw < ch ? cw : ch) / 24; if (r < 8) r = 8;   /* 球半径随画布缩放 */
    if (s_x < r) s_x = r;  if (s_x > cw - r) s_x = cw - r;
    if (s_y < r) s_y = r;  if (s_y > ch - r) s_y = ch - r;
    fx_canvas_clear(w, FX_RGB(250, 250, 250));

    /* 拖动: 按下且命中本画布时, 让球跟随手指 */
    int mx, my, mp;
    fx_touch_state(&mx, &my, &mp);
    if (mp && fx_pressed() == w) {
        int x1, y1; fx_widget_rect(w, &x1, &y1, 0, 0);
        s_x = mx - x1;
        s_y = my - y1;
        if (s_x < r) s_x = r;  if (s_x > cw - r) s_x = cw - r;
        if (s_y < r) s_y = r;  if (s_y > ch - r) s_y = ch - r;
    }

    fx_set_color(FX_BTN_BLUE);
    fx_fill_circle(s_x, s_y, r);

    char b[56];
    snprintf(b, sizeof b, "拖动我: (%d,%d)   按住画布即可拖", s_x, s_y);
    fx_draw_text_c(10, ch - 24, b, FX_UI_FG, FX_RGB(250, 250, 250));
}

static void on_reset(fx_widget_t *w, void *ud) { (void)w; (void)ud; s_x = 200; s_y = 100; }

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,232"), name("cv"),
                  color(FX_RGB(250,250,250)), anim(1), call(on_cv));
    fx_button_new(pixel("10,240", "120,266"), title("复位"), color(FX_BTN_BLUE), call(on_reset));
}
