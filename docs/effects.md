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

## v2.5：颜色透明度（FX_RGBA）

- `fx_color_t` 现在是 **ARGB**：`FX_RGBA(r,g,b,a)` 可给 1–255 的 alpha；`FX_RGB` 的 alpha=0 视为不透明，
  与旧代码完全兼容。
- 支持 alpha 的路径：GPU 填充/线段/圆角矩形/图片/文字贴图（sokol 管线本就开混合）。
- 文字专用 `FX_TEXT_NOBG`：让字形背景透明（stb 字体驱动识别）。
- 半透明不会改变 24 位老用例的金图结果（alpha=0 → 255）。

## 逐函数（`fxtk_effects.h` + `fxtk_image.h`）

| 函数 | 用法 |
|---|---|
| `fx_draw_image_rot(img,cx,cy,angle,scale)` | 以中心旋转绘制（CPU 路径）。 |
| `fx_fill_polygon_rot(pts,n,cx,cy,angle)` | 绕中心旋转填充多边形。 |
| `fx_image_flip_x(img)` / `fx_image_flip_y(img)` | 原地水平/垂直翻转。 |
| `fx_image_grayscale(img)` | 原地灰度化。 |
| `fx_image_tint(img,c,amount)` | 着色（amount 0–100）。 |
| `fx_image_brightness(img,delta)` | 亮度增减。 |
| `image(img)` | 属性宏：把图片绑到图片控件。 |
| `fx_image_create(w,h)` / `fx_image_free(img)` | 新建/释放内存图片。 |
| `fx_image_set_px(img,x,y,c)` | 写像素（程序化贴图）。 |
| `fx_image_load(path)` / `fx_image_set_zoom(w,pct)` / `fx_set_image(w,img)` | 加载 / 控件缩放 / 绑定控件。 |
| `fx_draw_image(img,x,y,dw,dh)` / `fx_draw_image_ex(...,dark)` | 缩放绘制 / 暗色版本。 |
| `fx_draw_image_quad(img,xy8)` / `fx_draw_image_quad_persp(...)` | 透视四边形贴图 / 快捷斜视屏幕。 |
| `fx_quad_homography(src8,dst8,m9)` / `fx_quad_corner_weights(xy8,d4)` / `fx_mat3_invert(m,out)` | 单应矩阵 / 四角权重 / 3×3 求逆。 |

> 完整签名见 [api.md](api.md) 第 10、17 节。
