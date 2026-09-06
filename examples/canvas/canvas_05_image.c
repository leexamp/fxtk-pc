/**
 * canvas/05_image — 在画布上渲染图片 (程序生成贴图 + 旋转/缩放/后处理)
 *
 * ★ 图片在 canvas 回调里也能画: fx_draw_image 自动遵守裁剪/离屏缓冲。
 *   - fx_image_create(w,h) + fx_image_set_px 程序生成任意贴图
 *   - fx_draw_image(img,x,y,dw,dh)     按目标矩形绘制 (可缩放)
 *   - fx_draw_image_rot(img,cx,cy,deg,pct) 以 (cx,cy) 为中心旋转/缩放
 *   - fx_image_grayscale / tint / brightness 原地后处理, 渲染前调一次
 */
#include "fxtk.h"
#include "fxtk_image.h"
#include "fxtk_effects.h"
#include <math.h>

static int s_tick = 0;
static fx_image_t *s_img = NULL;      /* 静态彩虹图 (左上) */
static fx_image_t *s_strip = NULL;    /* 长条图 (旋转 + 底部装饰) */
static fx_image_t *s_proc = NULL;     /* 灰度+染色后的图 (只处理一次, 避免原地修改污染) */

/* 让内容随画布缩放: 0~1 比例 -> 实际像素 */
#define X(f) ((int)(cw * (f)))
#define S(f) ((int)((cw < ch ? cw : ch) * (f)))

/* 程序生成一张彩虹渐变贴图 */
static fx_image_t *make_rainbow(int w, int h) {
    fx_image_t *img = fx_image_create(w, h);
    if (!img) return NULL;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            fx_color_t c = FX_RGB(x * 255 / (w - 1), y * 255 / (h - 1), 128);
            fx_image_set_px(img, x, y, c);
        }
    return img;
}

static void on_cv(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(250, 250, 250));

    if (!s_img)   s_img   = make_rainbow(64, 64);
    if (!s_strip) s_strip = make_rainbow(96, 32);
    if (!s_proc && (s_proc = make_rainbow(64, 64))) {
        fx_image_grayscale(s_proc);                     /* 原地后处理, 只做一次 */
        fx_image_tint(s_proc, FX_RGB(0, 200, 255), 120);
    }

    int m = X(0.03f);                          /* 外边距 */
    int tile = S(0.22f);                       /* 主图边长, 随画布缩放 */

    /* 1) 静态缩放贴图 (左上) */
    if (s_img) fx_draw_image(s_img, m, m, tile, tile);

    /* 2) 旋转贴图 (中部, 随 tick 转) */
    if (s_strip) fx_draw_image_rot(s_strip, cw / 2, m + tile / 2, (s_tick / 2) % 360, 100);

    /* 3) 灰度+染色后处理 (右上) */
    if (s_proc) fx_draw_image(s_proc, cw - m - tile, m, tile, tile);

    /* 4) 底部: 一行重复贴图做装饰 */
    int strip_w = S(0.10f), strip_h = S(0.07f); if (strip_w < 10) strip_w = 10; if (strip_h < 8) strip_h = 8;
    for (int x = m; x + strip_w <= cw - m; x += strip_w)
        if (s_strip) fx_draw_image(s_strip, x, ch - m - strip_h, strip_w, strip_h);

    fx_set_color(FX_UI_FG);
    fx_draw_text_c(m, ch - m - 12, "图像: 程序生成 + 旋转/缩放/灰度", FX_UI_FG, FX_RGB(250, 250, 250));

    s_tick++;
}

void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    fx_canvas_new(pixel("10,10", "470,262"), name("cv"),
                  color(FX_RGB(250,250,250)), anim(1), call(on_cv));
}
