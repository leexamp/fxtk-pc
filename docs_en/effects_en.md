# fxtk Image Effects

Effects operate on `fx_image_t` (`px` is a 24-bit RGB buffer, with `w/h`). All are pure CPU, pixel-level operations, suitable for programmatically generated surfaces, button state switching, and small demo assets.

## Creation and release (fxtk_image.h)

```c
fx_image_t *img = fx_image_create(w, h);    /* blank 24-bit RGB image */
fx_image_set_px(img, x, y, color);          /* fill pixel by pixel */
fx_image_free(img);                         /* release (must after use) */
```

## Rotated + scaled blitting

```c
void fx_draw_image_rot(fx_image_t *img, int cx, int cy, int angle_deg, int scale_pct);
```
Rotates counterclockwise around `(cx,cy)` by `angle_deg`, and scales by `scale_pct` (100 = original size). On PC it uses GPU hardware rotation + scaling (`SDL_RenderCopyEx`).

```c
fx_draw_image_rot(img, cw/2, ch/2, angle, 120);
```

## Scaled drawing

```c
void fx_draw_image(fx_image_t *img, int x, int y, int dw, int dh);
void fx_draw_image_ex(fx_image_t *img, int x, int y, int dw, int dh, int dark);
```
`dw/dh` are the target dimensions (GPU-scaled blit); `dark=1` darkens proportionally (press feedback).

## Pixel effects

```c
void fx_image_flip_x(fx_image_t *img);            /* horizontal mirror */
void fx_image_flip_y(fx_image_t *img);            /* vertical flip */
void fx_image_grayscale(fx_image_t *img);         /* grayscale */
void fx_image_tint(fx_image_t *img, fx_color_t c, int amount);   /* tint 0~255 */
void fx_image_brightness(fx_image_t *img, int delta);            /* brightness -255~255 */
```

## image widget

```c
fx_image_new(pixel("6,32","280,220"), name("pic"), image(img), call(on_img));
void fx_set_image(fx_widget_t *w, fx_image_t *img);      /* swap image */
void fx_image_set_zoom(fx_widget_t *w, int pct);         /* zoom 10~400% */
```

## Combined example

```c
fx_image_grayscale(pic);              /* process pixels first */
fx_draw_image_rot(pic, x, y, 45, 80); /* then rotate and blit */
fx_image_tint(pic, FX_RGB(0,200,255), 90);   /* tint to a cyan family */
```

> The offscreen canvas (3D page) goes through the `fxtk_put_px` pixel path, which does not interfere with the GPU text/geometry path.
