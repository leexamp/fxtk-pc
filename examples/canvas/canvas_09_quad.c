/**
 * canvas/09_quad — 四边形形变 (真透视映射)
 *
 * ★ 新特性 (v2.4): `fx_draw_image_quad(img, xy8)` 把整张图映射到任意凸四边形。
 *   四角顺时针: { 左上, 右上, 右下, 左下 }。这是**真透视**(单应矩阵 + 逆变换采样),
 *   不是"两个三角形"的仿射近似 —— 后者在对角线处会出现明显的缝与扭曲。
 *
 *   同一张棋盘图, 五种映射:
 *     1) 普通矩形 (对照组)
 *     2) 梯形: 顶部收窄 → 远小近大
 *     3) 强透视: 顶部收窄 + 水平偏移 (+ 垂直位移) → 地板/斜视平面
 *     4) 旋转四边形: 直接用四角表达任意旋转
 *     5) 斜切: 顶边平移 → 侧面墙面
 *
 *   注: 本示例无 GPU 时走 CPU 逐像素逆单应(ESP32 也是这条路径);
 *       有 GPU 时由驱动的 draw_image_quad 钩子接管(sokol 顶点着色器, P4)。
 */
#include "fxtk.h"
#include "fxtk_image.h"
#include <math.h>
#include <stdlib.h>

static fx_image_t *s_check = NULL;

/* 8x8 棋盘 + 边框 + 中心十字: 形变是否"正"一眼能看出来 */
static void make_checker(void)
{
    const int N = 8, CELL = 8;
    s_check = fx_image_create(N * CELL, N * CELL);
    if (!s_check) return;
    for (int y = 0; y < N * CELL; y++)
        for (int x = 0; x < N * CELL; x++) {
            int cx = x / CELL, cy = y / CELL;
            fx_color_t c = ((cx + cy) & 1) ? FX_RGB(240, 240, 240) : FX_RGB(40, 90, 160);
            if (x < 2 || y < 2 || x >= N * CELL - 2 || y >= N * CELL - 2) c = FX_RGB(230, 120, 40);   /* 外框 */
            if (abs(x - N * CELL / 2) < 2 || abs(y - N * CELL / 2) < 2) c = FX_RGB(200, 40, 60);     /* 十字 */
            fx_image_set_px(s_check, x, y, c);
        }
}

static void on_cv(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(24, 26, 32));
    if (!s_check) make_checker();
    if (!s_check) return;

    int m = cw / 40;
    int top = ch / 8, hgt = (ch * 3) / 8;
    /* v2.4: 统一格子 —— 5 格等宽 + 24px 间隙, 左右各留 m, 保证不越界 (斜切件曾被右边界裁掉) */
    int gap = cw / 20;
    int w0 = (cw - 2 * m - 4 * gap) / 5;
    if (w0 < 8) w0 = 8;
    int y1 = top, y2 = top + hgt;

    /* 1) 矩形 (对照) */
    fx_draw_image_ex(s_check, m, y1, w0, hgt, 0);
    fx_draw_text_c(m, y2 + 4, "矩形", FX_LGRAY, FX_RGB(24, 26, 32));

    /* 2) 梯形: 顶部收窄 35% */
    int x = m * 2 + w0;
    fx_draw_image_quad_persp(s_check, x, y1, x + w0, y2, 0.35f, 0.0f);
    fx_draw_text_c(x, y2 + 4, "梯形", FX_LGRAY, FX_RGB(24, 26, 32));

    /* 3) 强透视 + 偏移 (地板/斜视) */
    x = m * 3 + w0 * 2;
    fx_draw_image_quad_persp(s_check, x, y1 + hgt / 6, x + w0, y2, 0.55f, 0.18f);
    fx_draw_text_c(x, y2 + 4, "透视+偏移", FX_LGRAY, FX_RGB(24, 26, 32));

    /* 4) 旋转四边形 (四角直接给) */
    {
        x = m * 4 + w0 * 3;
        float cx = (float)x + w0 * 0.5f, cy = (float)(y1 + y2) * 0.5f;
        float rx = w0 * 0.5f, ry = hgt * 0.5f;
        float a = 0.42f;                                   /* 弧度 */
        float ca = cosf(a), sa = sinf(a);
        float ux[4] = { -rx, rx, rx, -rx }, uy[4] = { -ry, -ry, ry, ry };
        float q[8];
        for (int i = 0; i < 4; i++) {
            q[i * 2]     = cx + ux[i] * ca - uy[i] * sa;
            q[i * 2 + 1] = cy + ux[i] * sa + uy[i] * ca;
        }
        fx_draw_image_quad(s_check, q);
        fx_draw_text_c(x, y2 + 4, "旋转", FX_LGRAY, FX_RGB(24, 26, 32));
    }

    /* 5) 斜切 (顶边右移 = 侧面墙) */
    {
        x = m * 5 + w0 * 4;
        float sh = w0 * 0.22f;                 /* 顶边整体右移: 上下边等长 → 平行四边形 */
        float q[8] = {
            (float)x + sh,          (float)y1,
            (float)x + w0 + sh,     (float)y1,
            (float)x + w0,          (float)y2,
            (float)x,               (float)y2,
        };
        fx_draw_image_quad(s_check, q);
        fx_draw_text_c(x, y2 + 4, "斜切", FX_LGRAY, FX_RGB(24, 26, 32));
    }

    fx_draw_text_c(m, ch - m - 14, "fx_draw_image_quad: 真透视(单应) · 无 GPU 也走 CPU 逐像素",
                   FX_RGB(120, 200, 255), FX_RGB(24, 26, 32));
}

void app_init(void)
{
    fx_set_bg(FX_RGB(16, 18, 22));
    make_checker();
    fx_canvas_new(pixel("10,10", "470,262"), name("quad"), anim(1), call(on_cv));
}
