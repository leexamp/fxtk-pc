/**
 * fxtk_image.h — 图片功能扩展 (控件/属性类型已并入 fxtk.h 枚举)
 */
#ifndef FXTK_IMAGE_H
#define FXTK_IMAGE_H
#include "fxtk.h"

typedef struct fx_image {
    uint32_t *px;              /* 24bit RGB (0xRRGGBB) 像素 */
    int16_t w, h;
} fx_image_t;

fx_attr_t image(fx_image_t *img);              /* 属性构造器 */
#define fx_image_new(...) fx_widget_new_impl(FX_W_IMAGE, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})

fx_image_t *fx_image_create(int w, int h);     /* 创建空白图 (可用于离屏渲染) */
void fx_image_free(fx_image_t *img);
void fx_image_set_px(fx_image_t *img, int x, int y, fx_color_t c);
fx_image_t *fx_image_load(const char *path);   /* PC: SDL_image 解码 PNG/JPG/BMP */

/* 立即模式贴图 (canvas 回调里也能用, 自动遵守 clip/离屏缓冲) */
void fx_draw_image(fx_image_t *img, int x, int y, int dw, int dh);
void fx_draw_image_ex(fx_image_t *img, int x, int y, int dw, int dh, int dark);

void fx_set_image(fx_widget_t *w, fx_image_t *img);  /* 换图并立即重绘 */
void fx_image_set_zoom(fx_widget_t *w, int percent); /* 10~400, 100=铺满 */

/* ================= v2.4 四边形形变 (真透视) =================
 * 把整张图映射到任意凸四边形。四角按顺时针传入:
 *   xy8 = { x0,y0(左上), x1,y1(右上), x2,y2(右下), x3,y3(左下) } —— canvas/屏幕坐标。
 * - 有 GPU 时走驱动的透视四边形钩子 (顶点着色器做透视校正, P4);
 *   无 GPU(ESP32)或离屏画布时走 CPU 路径(逆单应 + 双线性采样)。
 * - 退化(面积≈0/自交)自动回退为包围盒矩形映射并告警一次, 绝不越界读写。
 */
void fx_draw_image_quad(const fx_image_t *img, const float *xy8);

/* 便捷: 矩形 + 参数化透视。top_inset = 顶部左右各内缩比例(0~1, 0.3 = 各缩 15% 宽),
 * top_shift = 顶部整体水平偏移比例。合起来就是伪 3D 里常见的"地板/墙面"四边形。 */
void fx_draw_image_quad_persp(const fx_image_t *img, int x1, int y1, int x2, int y2,
                              float top_inset, float top_shift);

/* 工具(也是测试入口): 求把 src8 四角映射到 dst8 四角的单应矩阵(行优先 3x3, m[8]=1);
 * 以及 3x3 求逆。成功返回 1, 退化返回 0。 */
int fx_quad_homography(const float *src8, const float *dst8, float m[9]);
/* v2.4 P4: 给 GPU 真透视四边形用的【四角裁剪权重】(即单应的分母 d_i)。
 * 驱动把 d_i 直接写进 gl_Position.w, 硬件就会按透视校正插值 uv —— 单次 draw 两个三角形即
 * 透视正确, 不会出现"两个三角形各做仿射"的对角缝(这是该问题的正解)。
 * 权重来自 H: 单位方 → 目标四边形, d(x,y)=g*x+h*y+1, 四角 (0,0)(1,0)(1,1)(0,1) 对应
 * d = 1, g+1, g+h+1, h+1。退化(某角灭点落在角上)返回 0。 */
int fx_quad_corner_weights(const float *xy8, float d4[4]);
int fx_mat3_invert(const float *m, float out[9]);
#endif
