/**
 * canvas/10_transform — canvas 变换栈 (2D 仿射) + 实心四边形
 *
 * ★ 新特性 (v2.4):
 *   `fx_canvas_push_affine(a,b,c,d,e,f)` / `fx_canvas_pop_affine()`
 *   语义: p' = M·p, M = [[a,c,e],[b,d,f],[0,0,1]]; 后 push 的变换在【外层】
 *   (像场景图那样嵌套)。push 之后:
 *     - fx_fill_rect  → 变成任意旋转/斜切的实心四边形 (fx_fill_quad 内核)
 *     - fx_draw_image_ex / fx_draw_image → 走透视四边形路径(旋转/缩放/斜切一次到位)
 *     - fx_draw_pixel / fx_draw_line → 端点过变换
 *   栈深 8, 每帧自动复位。
 *
 *   本示例: 斜切"墙面"(实心四边形) + 旋转贴图 + 平铺地板 + 变换后的网格线。
 */
#include "fxtk.h"
#include "fxtk_image.h"
#include <math.h>
#include <stdlib.h>

static fx_image_t *s_tex = NULL;

static void make_tex(void)
{
    const int N = 64;
    s_tex = fx_image_create(N, N);
    if (!s_tex) return;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int g = ((x / 8 + y / 8) & 1) ? 200 : 70;
            fx_color_t c = FX_RGB(g, (uint8_t)(g * 0.8f), (uint8_t)(g * 0.6f));
            if (x % 8 == 0 || y % 8 == 0) c = FX_RGB(30, 34, 40);       /* 网格线 */
            fx_image_set_px(s_tex, x, y, c);
        }
}

static void on_cv(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(18, 20, 26));
    if (!s_tex) make_tex();
    if (!s_tex) return;

    int m = cw / 40;

    /* 1) 斜切实心"墙面": 普通矩形在斜切变换下自动变四边形 */
    fx_set_color(FX_RGB(60, 90, 150));
    fx_canvas_push_affine(1.0f, 0.0f, 0.35f, 1.0f, (float)m, (float)m);   /* 水平斜切 */
    fx_fill_rect(0, 0, cw / 5, ch / 3);
    fx_canvas_pop_affine();
    fx_draw_text_c(m, m + ch / 3 + 2, "斜切墙面(fill_rect)", FX_LGRAY, FX_RGB(18, 20, 26));

    /* 2) 旋转贴图: 同一张纹理, 变换不同角度 */
    for (int i = 0; i < 3; i++) {
        float ang = 0.25f * (float)(i + 1);
        float ca = cosf(ang), sa = sinf(ang);
        float cx = (float)(cw * 2 / 5 + i * (cw / 7));
        float cy = (float)(ch / 4);
        fx_canvas_push_affine(ca, sa, -sa, ca, cx, cy);
        fx_draw_image_ex(s_tex, -(cw / 16), -(ch / 8), cw / 8, ch / 4, 0);
        fx_canvas_pop_affine();
    }
    fx_draw_text_c(cw * 2 / 5, ch / 2 + 4, "旋转贴图(push_affine+draw_image)", FX_LGRAY, FX_RGB(18, 20, 26));

    /* 3) 平铺地板: 用变换栈做 2D 平铺 (每格一次 push 平移) */
    {
        int tw = cw / 8, th = ch / 5;
        for (int ty = 0; ty < 2; ty++)
            for (int tx = 0; tx < 8; tx++) {
                fx_canvas_push_affine(1, 0, 0, 1, (float)(m + tx * tw), (float)(ch * 2 / 3 + ty * th));
                fx_draw_image_ex(s_tex, 0, 0, tw, th, (tx + ty) & 1 ? 1 : 0);
                fx_canvas_pop_affine();
            }
        fx_draw_text_c(m, ch * 2 / 3 - 14, "平铺地板(变换栈)", FX_LGRAY, FX_RGB(18, 20, 26));
    }

    /* 4) 变换后的网格线 */
    {
        float ca = cosf(0.5f), sa = sinf(0.5f);
        fx_set_color(FX_RGB(120, 200, 255));
        fx_canvas_push_affine(ca, sa, -sa, ca, (float)(cw - m * 3), (float)(ch / 2));
        for (int i = -3; i <= 3; i++) {
            fx_draw_line(i * 6, -40, i * 6, 40);
            fx_draw_line(-40, i * 6, 40, i * 6);
        }
        fx_canvas_pop_affine();
    }
}

void app_init(void)
{
    fx_set_bg(FX_RGB(14, 16, 20));
    make_tex();
    fx_canvas_new(pixel("10,10", "470,262"), name("xf"), anim(1), call(on_cv));
}
