/**
 * canvas/02_immediate — 立即模式与 anim(1) 每帧回调
 *
 * ★ 立即模式: 你不需要维护一棵控件树, 而是【每一帧在回调里重画整幅场景】。
 *   - anim(1) 让回调被框架每帧调用 (游戏/动画/雷达/图表都这么写)
 *   - 用静态变量存状态 (s_tick), 每次回调读取并更新
 *
 * ★ 内容要随画布缩放: 半径 R 取 cw/ch 的比例, 否则窗口拖大后表盘很小。
 * 本示例画一个大号时钟: 表盘 + 12 刻度 + 秒针(随 s_tick 转) + 中心轴。
 */
#include "fxtk.h"
#include <stdio.h>

#define PI 3.14159265f

static int s_tick = 0;    /* 自增帧计数 */

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(245, 245, 245));

    int cx = cw / 2, cy = ch / 2;
    int R = (cw < ch ? cw : ch) * 2 / 5;          /* 表盘半径 = 短边 40%, 占满画布而不溢出 */
    if (R < 8) R = 8;

    fx_set_color(FX_UI_FG);
    fx_draw_circle(cx, cy, R);

    /* 12 个刻度 (短线, 每 30°) */
    for (int i = 0; i < 12; i++) {
        float a = i * 30.0f * PI / 180.0f;
        int t1x = cx + (int)((R - R * 0.18f) * sinf(a));
        int t1y = cy - (int)((R - R * 0.18f) * cosf(a));
        int t2x = cx + (int)(R * sinf(a));
        int t2y = cy - (int)(R * cosf(a));
        fx_draw_line(t1x, t1y, t2x, t2y);
    }

    /* 秒针: 每 60 帧转一圈, 指向随 s_tick */
    float ang = (s_tick % 60) * PI / 30.0f;       /* 2pi/60 */
    int sx = cx + (int)(R * 0.78f * sinf(ang));
    int sy = cy - (int)(R * 0.78f * cosf(ang));
    fx_set_color(FX_RED_ACCENT);
    fx_draw_line(cx, cy, sx, sy);

    fx_fill_circle(cx, cy, R / 12 + 2);           /* 中心轴 */

    char b[40];
    snprintf(b, sizeof b, "tick=%d", s_tick);
    fx_draw_text_c(8, 8, b, FX_UI_FG, FX_RGB(245, 245, 245));

    s_tick++;   /* 下一帧的状态 */
}

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,262"), name("cv"),
                  color(FX_RGB(245,245,245)), anim(1), call(on_cv));
}
