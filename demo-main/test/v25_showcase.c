/**
 * v25_showcase.c — v2.5 新功能可视化演示 (只提供 app_init, 与 render_canvas.c 链接)
 *
 * 【这个文件是干什么的】
 *   CHANGELOG 里写"已实现"不算数。这里用【真实渲染管线】把 v2.5 的视觉功能画出来,
 *   导出 PNG, 让人直接看像素。
 *
 * 【⚠️ 第一个坑: 子控件是【头插】的, 迭代顺序 = 逆创建顺序】
 *   fxtk_link() 的实现是:
 *       child->sibling = parent->child;  parent->child = child;
 *   所以【最后创建的控件排在链首】。而 fx_flex_apply / layout_children / 绘制
 *   全都按 child -> sibling 迭代 —— 也就是【逆着创建顺序】。
 *
 *   我第一版按直觉"先建的排前面"写, 结果三个面板的上下顺序整个反了。
 *   本文件里的创建顺序是【故意反着写的】, 并在下面标了注释。
 *
 *   (框架里全是一致的, 不是 flex 独有的 bug; 但对使用者是个真陷阱。
 *    已在 fxtk.h 的 fx_flex_apply 注释里写明。)
 */
#include "fxtk.h"
#include <stdlib.h>
#include <string.h>

/* ===================== SVG 源 (真实 SVG 文本) ===================== */

/* 心形: 三次贝塞尔 C 命令 */
static const char *SVG_HEART =
    "<svg viewBox=\"0 0 24 24\">"
    "  <path d=\"M12 21 C5 15 2 11 2 7.5 C2 4.5 4.5 2 7.5 2 C9.5 2 11 3 12 5 "
    "C13 3 14.5 2 16.5 2 C19.5 2 22 4.5 22 7.5 C22 11 19 15 12 21 Z\" "
    "       fill=\"#e8434f\"/>"
    "</svg>";

/* 五角星: polygon */
static const char *SVG_STAR =
    "<svg viewBox=\"0 0 24 24\">"
    "  <polygon points=\"12,2 15,9 22,9 16,14 18,21 12,17 6,21 8,14 2,9 9,9\" "
    "           fill=\"#f5c542\"/>"
    "</svg>";

/* 圆环: circle 只描边不填充 */
static const char *SVG_RING =
    "<svg viewBox=\"0 0 24 24\">"
    "  <circle cx=\"12\" cy=\"12\" r=\"9\" fill=\"none\" stroke=\"#43b0f1\" stroke-width=\"3\"/>"
    "  <circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"#43b0f1\"/>"
    "</svg>";

/* 圆角方块 + transform 位移的嵌套小方 */
static const char *SVG_BOX =
    "<svg viewBox=\"0 0 24 24\">"
    "  <rect x=\"2\" y=\"2\" width=\"20\" height=\"20\" rx=\"5\" fill=\"#3ddc84\"/>"
    "  <g transform=\"translate(7,7)\">"
    "    <rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#0b3d20\"/>"
    "  </g>"
    "</svg>";

static fx_svg_t *s_heart, *s_star, *s_ring, *s_box;

/* ===================== 上栏: 并行 for ===================== */

#define PARW 200
#define PARH 40
static unsigned char s_grad[PARW * PARH * 3];
static int s_par_done = 0;

/* fx_parallel_for 的回调: 只碰自己那一段 [lo,hi), 无共享写 */
static void grad_body(int lo, int hi, void *ud)
{
    (void)ud;
    for (int y = lo; y < hi; y++) {
        for (int x = 0; x < PARW; x++) {
            unsigned char *p = &s_grad[(y * PARW + x) * 3];
            p[0] = (unsigned char)(x * 255 / (PARW - 1));   /* R 横向 */
            p[1] = (unsigned char)(y * 255 / (PARH - 1));   /* G 纵向 */
            p[2] = (unsigned char)(((x + y) * 255) / (PARW + PARH - 2));
        }
    }
}

static void cv_par(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    if (cw < 8 || ch < 8) return;

    /* 每次调用都并行重算一遍 —— 结果确定, 便于反复观察 */
    fx_parallel_for(PARH, grad_body, NULL);
    s_par_done = 1;

    /* 整块铺深底, 免得留白看起来像没画 */
    fx_set_color(FX_RGB(22, 22, 30));
    fx_fill_rect(0, 0, cw - 1, ch - 1);

    /* 左 55%: 逐像素画渐变 —— 分块边界若算错, 这里会看到横向接缝 */
    int bw = cw * 55 / 100;
    int bh = ch - 8;
    if (bh < 1) bh = 1;
    for (int y = 0; y < bh; y++) {
        int sy = y * PARH / bh;
        for (int x = 0; x < bw; x++) {
            int sx = x * PARW / bw;
            const unsigned char *p = &s_grad[(sy * PARW + sx) * 3];
            fx_set_color(FX_RGB(p[0], p[1], p[2]));
            fx_draw_pixel(4 + x, 4 + y);
        }
    }

    /* 右: 线程数条 —— 【一格 = 一个线程】, 数格子就知道开了几个 */
    int nt = fx_parallel_threads();
    if (nt <= 0) nt = 1;
    if (nt > 24) nt = 24;
    int bx = bw + 14;
    for (int i = 0; i < nt; i++) {
        fx_set_color(FX_RGB(120, 220, 255));
        int x0 = bx + i * 4;
        if (x0 + 2 >= cw) break;
        fx_fill_rect(x0, 6, x0 + 2, ch - 7);
    }
}

/* ===================== 中栏: SVG ===================== */

static void cv_svg(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    if (cw < 8 || ch < 8) return;

    fx_set_color(FX_RGB(28, 28, 38));
    fx_fill_rect(0, 0, cw - 1, ch - 1);

    int n = 4;
    int cellw = cw / n;
    int ih = ch - 12;
    int iw = ih;
    if (iw > cellw - 12) iw = cellw - 12;
    if (iw < 1 || ih < 1) return;

    const fx_svg_t *icons[4] = { s_heart, s_star, s_ring, s_box };
    for (int i = 0; i < n; i++) {
        int cx = i * cellw + (cellw - iw) / 2;
        int cy = (ch - ih) / 2;
        fx_set_color(FX_RGB(52, 52, 68));
        fx_draw_rect(cx - 2, cy - 2, cx + iw + 1, cy + ih + 1);   /* 图标外框 */
        if (icons[i]) fx_svg_draw(icons[i], cx, cy, iw, ih);
    }
}

/* ===================== 下栏: 布局占用 (flex 的证明) ===================== */

static void cv_fill(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    if (cw < 4 || ch < 4) return;

    /* 整块铺满橙色 —— 只要 flex 生效, 橙色就会一直铺到画面最底部 */
    fx_set_color(FX_RGB(224, 112, 58));
    fx_fill_rect(0, 0, cw - 1, ch - 1);

    /* 左上角一个【边长 = 本面板高度】的正方形: 数它就能反推 flex 给的高度 */
    int sq = ch - 4;
    if (sq > cw - 4) sq = cw - 4;
    if (sq < 1) sq = 1;
    fx_set_color(FX_RGB(255, 255, 255));
    fx_draw_rect(2, 2, 2 + sq - 1, 2 + sq - 1);

    /* 每 10px 一个刻度 —— 数格子 = 数像素 */
    fx_set_color(FX_RGB(120, 45, 10));
    for (int y = 10; y < ch - 2; y += 10) fx_draw_hline(2, 14, y);
}

/* ===================== app_init ===================== */

void app_init(void)
{
    s_heart = fx_svg_parse(SVG_HEART);
    s_star  = fx_svg_parse(SVG_STAR);
    s_ring  = fx_svg_parse(SVG_RING);
    s_box   = fx_svg_parse(SVG_BOX);

    fx_set_bg(FX_RGB(16, 16, 22));

    /* 顶层容器: 百分比铺满, 换分辨率也能看 flex 适应 */
    fx_widget_t *root = fx_panel_new(percent("0,0", "100,100"),
                                     color(FX_RGB(16, 16, 22)), name("root"));
    fx_parent(root);

    /* ⚠️ 创建顺序【故意反着写】: 子控件是头插的, 最后创建的排最前。
     *    所以想让画面从上到下是 par / svg / fill, 就要【倒着创建】。 */
    fx_canvas_new(percent("0,0", "100,20"), name("band_fill"), call(cv_fill));
    fx_canvas_new(percent("0,0", "100,20"), name("band_svg"),  call(cv_svg));
    fx_canvas_new(percent("0,0", "100,20"), name("band_par"),  call(cv_par));

    /* 让 percent 先算容器真实矩形, 否则 flex 拿到的是未布局的 0 尺寸 */
    fx_layout();

    /* ★ flex 纵排: 前两块固定高, 第三块 grow=1 吃掉全部剩余
     *   480x272: pad6/gap6 → 内容 260, 减 gap12 = 248; 基础 180 → 剩余 68 全给第三块
     *   结果 60 / 80 / 108
     *   若 flex 失效: 第三块只有 40, 【画面底部会空出 68px】—— 一眼可辨 */
    fx_flex_item_t it[3] = {
        { .size = 60, .grow = 0 },   /* 给链首 = par 栏 */
        { .size = 80, .grow = 0 },   /* 给第二个 = svg 栏 */
        { .size = 40, .grow = 1 },   /* 给链尾 = fill 栏, 吃掉剩余 */
    };
    fx_flex_apply(root, it, 3, FX_FLEX_COL, 6, 6, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH);
}
