/**
 * fxtk_effects.h — 渲染引擎扩展: 旋转 / 图像后处理 / 旋转矢量
 */
#ifndef FXTK_EFFECTS_H
#define FXTK_EFFECTS_H
#include "fxtk_image.h"

/* 旋转贴图: 以 (cx,cy) 为中心, angle_deg 逆时针, scale_pct 缩放(100=原尺寸) */
void fx_draw_image_rot(fx_image_t *img, int cx, int cy, int angle_deg, int scale_pct);

/* 旋转填充多边形: pts 以原点为中心, 绕 (cx,cy) 旋转 angle_deg 后填充 */
void fx_fill_polygon_rot(const int16_t *pts, int n, int cx, int cy, int angle_deg);

/* 图像后处理 (原地修改, 渲染前调用一次即可) */
void fx_image_flip_x(fx_image_t *img);              /* 水平镜像 */
void fx_image_flip_y(fx_image_t *img);              /* 垂直翻转 */
void fx_image_grayscale(fx_image_t *img);           /* 灰度 */
void fx_image_tint(fx_image_t *img, fx_color_t c, int amount);      /* 染色 0~255 */
void fx_image_brightness(fx_image_t *img, int delta);               /* 亮度 -255~255 */
#endif
