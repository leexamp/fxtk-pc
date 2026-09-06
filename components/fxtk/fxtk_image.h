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
#endif
