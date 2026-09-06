/** ex03_anim — anim 画布: 波形 + 旋转矢量 */
#include "fxtk.h"
#include "fxtk_effects.h"
#include <math.h>
static int s_t = 0;
static void on_cv(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(0x080808); fx_fill_rect(0, 0, cw - 1, ch - 1);
    int amp = ch / 2 - 10, prev = ch / 2;
    fx_set_color(FX_YELLOW);
    for (int x = 0; x < cw; x++) {
        int y = ch / 2 + (int)(amp * sin((x + s_t) * 3.14159 / 24.0));
        fx_draw_line(x - 1, prev, x, y); prev = y;
    }
    int16_t tri[6] = { 0, -30, 26, 15, -26, 15 };
    fx_set_color(FX_MAGENTA);
    fx_fill_polygon_rot(tri, 3, cw / 2, ch / 2, s_t % 360);
    s_t += 2;
}
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_canvas_new(pixel("10,10", "470,262"), anim(1), color(0x080808), call(on_cv));
}
