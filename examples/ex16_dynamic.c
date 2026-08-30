/** ex16_dynamic — 运行时动态创建控件 + 固定坐标 + 漂移 (压测浓缩版) */
#include "fxtk.h"
#include <math.h>
#include <stdio.h>

#define DYN_MAX 200
static fx_widget_t *s_dyn[DYN_MAX];
static int s_n = 0, s_t = 0;

static void on_btn(fx_widget_t *w, void *ud) { (void)w; (void)ud; }   /* 动态按钮的点击回调(空) */

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_RGB(20, 20, 26)); fx_fill_rect(0, 0, cw - 1, ch - 1);
    s_t++;

    /* 帧率富余时每帧 +1 个按钮 (最多 200) */
    if (fxtk_fps() >= 30 && s_n < DYN_MAX) {
        fx_parent(w);
        fx_widget_t *nb = fx_button_new(pixel("0,0", "0,0"), title("动态"),
                                        line(9), color(FX_RGB(156, 39, 176)), call(on_btn));
        fx_parent(NULL);
        if (nb) {
            int nx = 8 + (s_n % 20) * 22, ny = 28 + (s_n / 20) * 20;
            fx_widget_fix(nb, x1 + nx, y1 + ny);   /* 固定坐标防布局复位 */
            fx_widget_set_rect(nb, x1 + nx, y1 + ny, x1 + nx + 20, y1 + ny + 16);
            s_dyn[s_n++] = nb;
        }
    }
    /* 所有动态按钮正弦漂移 */
    for (int i = 0; i < s_n; i++) {
        fx_widget_t *d = s_dyn[i];
        int bx = x1 + 8 + (i % 20) * 22 + (int)(12 * sinf(s_t * 0.05f + i * 0.7f));
        int by = y1 + 28 + (i / 20) * 20 + (int)(8 * cosf(s_t * 0.04f + i * 1.1f));
        fx_widget_set_rect(d, bx, by, bx + 20, by + 16);
    }
    char b[48];
    snprintf(b, sizeof(b), "动态控件 %d 个 (共 %d)", s_n, fxtk_widget_count());
    fx_draw_text_c(10, 6, b, FX_GREEN, FX_RGB(20, 20, 26));
}

void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_canvas_new(pixel("10,10", "470,262"), anim(1), call(on_cv));
}
