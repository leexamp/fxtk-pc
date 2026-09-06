/**
 * fxtk_effects.c — 旋转/后处理实现 (逆映射采样, 自动遵守 clip 与离屏缓冲)
 */
#include "fxtk_effects.h"
#include <math.h>

void fxtk_put_px(int x, int y, uint32_t c);   /* fxtk_draw.c */

/* 旋转贴图: 对包围盒内每个屏幕像素, 反向旋转回源图采样 (最近邻) */
void fx_draw_image_rot(fx_image_t *img, int cx, int cy, int angle_deg, int scale_pct)
{
    extern int fxtk_image_rot_gpu(const fx_image_t*,int,int,int,int,double);
    if (fxtk_image_rot_gpu(img,cx,cy,scale_pct,scale_pct,(double)angle_deg)) return;   /* v2: GPU旋转 */


    if (!img || !img->px || scale_pct <= 0) return;
    float rad = (float)angle_deg * 3.14159265f / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    float sc = scale_pct / 100.0f;
    float hw = img->w * 0.5f * sc, hh = img->h * 0.5f * sc;
    int Rx = (int)(hw * fabsf(cs) + hh * fabsf(sn)) + 1;
    int Ry = (int)(hw * fabsf(sn) + hh * fabsf(cs)) + 1;
    float icx = img->w * 0.5f, icy = img->h * 0.5f;
    for (int j = -Ry; j <= Ry; j++) {
        for (int i = -Rx; i <= Rx; i++) {
            int sx = (int)((i * cs + j * sn) / sc + icx);
            int sy = (int)((-i * sn + j * cs) / sc + icy);
            if (sx < 0 || sy < 0 || sx >= img->w || sy >= img->h) continue;
            fxtk_put_px(cx + i, cy + j, img->px[sy * img->w + sx]);
        }
    }
}

void fx_fill_polygon_rot(const int16_t *pts, int n, int cx, int cy, int angle_deg)
{
    if (n < 3 || n > 32) return;
    float rad = (float)angle_deg * 3.14159265f / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    int16_t out[64];
    for (int i = 0; i < n; i++) {
        float x = pts[i * 2], y = pts[i * 2 + 1];
        out[i * 2]     = (int16_t)(cx + x * cs - y * sn);
        out[i * 2 + 1] = (int16_t)(cy + x * sn + y * cs);
    }
    fx_fill_polygon(out, n);
}

void fx_image_flip_x(fx_image_t *img)
{
    if (!img) return;
    for (int y = 0; y < img->h; y++)
        for (int x = 0; x < img->w / 2; x++) {
            uint32_t *a = &img->px[y * img->w + x];
            uint32_t *b = &img->px[y * img->w + img->w - 1 - x];
            uint32_t t = *a; *a = *b; *b = t;
        }
}

void fx_image_flip_y(fx_image_t *img)
{
    if (!img) return;
    for (int y = 0; y < img->h / 2; y++)
        for (int x = 0; x < img->w; x++) {
            uint32_t *a = &img->px[y * img->w + x];
            uint32_t *b = &img->px[(img->h - 1 - y) * img->w + x];
            uint32_t t = *a; *a = *b; *b = t;
        }
}

void fx_image_grayscale(fx_image_t *img)
{
    if (!img) return;
    for (int i = 0; i < img->w * img->h; i++) {
        uint32_t c = img->px[i];
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        int y8 = (r * 77 + g * 150 + b * 29) >> 8;
        img->px[i] = (uint32_t)((y8 << 16) | (y8 << 8) | y8);
    }
}

void fx_image_tint(fx_image_t *img, fx_color_t color, int amount)
{
    if (!img || amount <= 0) return;
    if (amount > 255) amount = 255;
    int tr = (color >> 16) & 0xFF, tg = (color >> 8) & 0xFF, tb = color & 0xFF;
    for (int i = 0; i < img->w * img->h; i++) {
        uint32_t c = img->px[i];
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        r = (r * (255 - amount) + tr * amount) / 255;
        g = (g * (255 - amount) + tg * amount) / 255;
        b = (b * (255 - amount) + tb * amount) / 255;
        img->px[i] = (uint32_t)((r << 16) | (g << 8) | b);
    }
}

void fx_image_brightness(fx_image_t *img, int delta)
{
    if (!img) return;
    for (int i = 0; i < img->w * img->h; i++) {
        uint32_t c = img->px[i];
        int r = ((c >> 16) & 0xFF) + delta;
        int g = ((c >> 8) & 0xFF) + delta;
        int b = (c & 0xFF) + delta;
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        img->px[i] = (uint32_t)((r << 16) | (g << 8) | b);
    }
}
