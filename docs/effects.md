# fxtk 图像特效

特效作用于 `fx_image_t`（`px` 为 24bit RGB 缓冲，含 `w/h`）。全部为纯 CPU 像素级，适合程序生成贴图、按钮态切换、小型演示素材。

## 创建与释放（fxtk_image.h）

```c
fx_image_t *img = fx_image_create(w, h);    /* 空白 24bit RGB 图 */
fx_image_set_px(img, x, y, color);          /* 逐像素填充 */
fx_image_free(img);                         /* 释放 (用完必须) */
```

## 旋转 + 缩放贴画

```c
void fx_draw_image_rot(fx_image_t *img, int cx, int cy, int angle_deg, int scale_pct);
```
以 `(cx,cy)` 为中心，`angle_deg` 逆时针旋转，`scale_pct` 缩放（100=原尺寸）。PC 端走 GPU 硬件旋转+缩放（`SDL_RenderCopyEx`）。

```c
fx_draw_image_rot(img, cw/2, ch/2, angle, 120);
```

## 缩放绘制

```c
void fx_draw_image(fx_image_t *img, int x, int y, int dw, int dh);
void fx_draw_image_ex(fx_image_t *img, int x, int y, int dw, int dh, int dark);
```
`dw/dh` 为目标尺寸（GPU 缩放 blit）；`dark=1` 按压缩暗（按压反馈）。

## 像素特效

```c
void fx_image_flip_x(fx_image_t *img);            /* 水平镜像 */
void fx_image_flip_y(fx_image_t *img);            /* 垂直翻转 */
void fx_image_grayscale(fx_image_t *img);         /* 灰度 */
void fx_image_tint(fx_image_t *img, fx_color_t c, int amount);   /* 染色 0~255 */
void fx_image_brightness(fx_image_t *img, int delta);            /* 亮度 -255~255 */
```

## image 控件

```c
fx_image_new(pixel("6,32","280,220"), name("pic"), image(img), call(on_img));
void fx_set_image(fx_widget_t *w, fx_image_t *img);      /* 换图 */
void fx_image_set_zoom(fx_widget_t *w, int pct);         /* 缩放 10~400% */
```

## 组合示例

```c
fx_image_grayscale(pic);              /* 先处理像素 */
fx_draw_image_rot(pic, x, y, 45, 80); /* 再旋转贴出 */
fx_image_tint(pic, FX_RGB(0,200,255), 90);   /* 染色成青色系 */
```

> 离屏画布（3D 页）走 `fxtk_put_px` 像素路径，与 GPU 文本/几何路径互不干扰。
