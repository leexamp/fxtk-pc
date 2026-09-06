/** ex06_img — 图片资源 + 旋转贴图 (渲染引擎能力) */
#include "fxtk.h"
#include "fxtk_image.h"
#include "fxtk_effects.h"
static fx_image_t *s_img;
static int s_t;
static void on_cv(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(0x000083); fx_fill_rect(0, 0, cw - 1, ch - 1);
    s_t++;
    fx_draw_image_rot(s_img, cw / 2, ch / 2, s_t % 360, 120);
}
static fx_image_t *make_img(void) {
    fx_image_t *im = fx_image_create(96, 96);
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            int dx = x - 48, dy = y - 48;
            fx_color_t c = (dx*dx + dy*dy < 1600) ? FX_RGB(244, 67, 54)
                         : ((x/12 + y/12) & 1 ? FX_RGB(33, 150, 243) : FX_RGB(240, 240, 240));
            fx_image_set_px(im, x, y, c);
        }
    return im;
}
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    s_img = make_img();
    fx_canvas_new(pixel("10,10", "470,262"), anim(1), color(0x000083), call(on_cv));
}
