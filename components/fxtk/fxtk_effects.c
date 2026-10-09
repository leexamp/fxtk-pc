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
    /* v2.4.4 修复(第三方评审 B3 + 一处更硬的错):
     * ① 判空必须在使用之前 —— 原先把 GPU 钩子调用放在判空前面, 空指针会先进驱动。
     * ② 钩子的 dw/dh 语义是【目标尺寸】(两个驱动都是 dst={...,dw,dh} / emit_quad(dx..dx+dw)),
     *    而这里原先传的是 scale_pct/scale_pct(百分比!) → GPU 把 90 当成 90x90 像素、
     *    CPU 路径当 90%, 同一参数两种结果, 非方图还会丢长宽比 —— 直接破坏"两端渲染一致"的承诺。
     *    现按与 CPU 路径相同的含义换算成目标尺寸再传。 */
    if (!img || !img->px || scale_pct <= 0) return;
    int dw = (int)(img->w * scale_pct / 100.0f + 0.5f);
    int dh = (int)(img->h * scale_pct / 100.0f + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (fxtk_image_rot_gpu(img, cx, cy, dw, dh, (double)angle_deg)) return;   /* v2: GPU旋转 */
    float rad = (float)angle_deg * 3.14159265f / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    float sc = scale_pct / 100.0f;
    float hw = img->w * 0.5f * sc, hh = img->h * 0.5f * sc;
    int Rx = (int)(hw * fabsf(cs) + hh * fabsf(sn)) + 1;
    int Ry = (int)(hw * fabsf(sn) + hh * fabsf(cs)) + 1;
    float icx = img->w * 0.5f, icy = img->h * 0.5f;
    /* v2.4.5 性能: 行不变量提升 —— j*sn / j*cs 每行只算一次(原先每像素 2 次乘法),
     * img->w/h/px 提到局部(put_px 是外部调用, 编译器不敢缓存它们, 每像素重载)。
     * 浮点运算顺序与原实现完全一致(同样的乘法, 只是少了重复), 输出逐位不变。 */
    int iw = img->w, ih = img->h;
    const uint32_t *ipx = img->px;
    for (int j = -Ry; j <= Ry; j++) {
        float rj_sn = j * sn, rj_cs = j * cs;
        for (int i = -Rx; i <= Rx; i++) {
            int sx = (int)((i * cs + rj_sn) / sc + icx);
            int sy = (int)((-i * sn + rj_cs) / sc + icy);
            if (sx < 0 || sy < 0 || sx >= iw || sy >= ih) continue;
            fxtk_put_px(cx + i, cy + j, ipx[sy * iw + sx]);
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
    /* v2.4.5 性能: 行指针步进 + 双指针对撞 (原先每像素重算 y*w 与 w-1-x) */
    int w = img->w, h = img->h;
    for (int y = 0; y < h; y++) {
        uint32_t *row = img->px + (size_t)y * w;
        for (int x = 0, xr = w - 1; x < xr; x++, xr--) {
            uint32_t t = row[x]; row[x] = row[xr]; row[xr] = t;
        }
    }
}

void fx_image_flip_y(fx_image_t *img)
{
    if (!img) return;
    /* v2.4.5 性能: 整行对撞交换 (原先每像素重算两个 y*w 偏移) */
    int w = img->w, h = img->h;
    for (int y = 0, yb = h - 1; y < yb; y++, yb--) {
        uint32_t *a = img->px + (size_t)y * w;
        uint32_t *b = img->px + (size_t)yb * w;
        for (int x = 0; x < w; x++) {
            uint32_t t = a[x]; a[x] = b[x]; b[x] = t;
        }
    }
}

void fx_image_grayscale(fx_image_t *img)
{
    if (!img) return;
    int n = img->w * img->h;
    uint32_t *px = img->px;   /* v2.4.5: 指针步进, 免去每像素的 img-> 重载与 w*h 重复判断 */
    for (int i = 0; i < n; i++) {
        uint32_t c = px[i];
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        int y8 = (r * 77 + g * 150 + b * 29) >> 8;
        px[i] = (uint32_t)((y8 << 16) | (y8 << 8) | y8);
    }
}

void fx_image_tint(fx_image_t *img, fx_color_t color, int amount)
{
    if (!img || amount <= 0) return;
    if (amount > 255) amount = 255;
    int tr = (color >> 16) & 0xFF, tg = (color >> 8) & 0xFF, tb = color & 0xFF;
    /* v2.4.5 性能: 每通道 256 项 LUT —— 对固定的 amount/染色, 新值只取决于原通道值,
     * 768 次预计算替代每像素 3 次(乘+除); LUT 用与原循环完全相同的公式, 输出逐位一致。 */
    int inv = 255 - amount;
    uint8_t lut_r[256], lut_g[256], lut_b[256];
    for (int v = 0; v < 256; v++) {
        lut_r[v] = (uint8_t)((v * inv + tr * amount) / 255);
        lut_g[v] = (uint8_t)((v * inv + tg * amount) / 255);
        lut_b[v] = (uint8_t)((v * inv + tb * amount) / 255);
    }
    int n = img->w * img->h;
    uint32_t *px = img->px;
    for (int i = 0; i < n; i++) {
        uint32_t c = px[i];
        px[i] = ((uint32_t)lut_r[(c >> 16) & 0xFF] << 16)
              | ((uint32_t)lut_g[(c >> 8) & 0xFF] << 8)
              |  (uint32_t)lut_b[c & 0xFF];
    }
}

void fx_image_brightness(fx_image_t *img, int delta)
{
    if (!img) return;
    /* v2.4.5 性能: clamp(v+delta) 只取决于 v —— 一次 256 项 LUT 替代每像素 6 次比较 */
    uint8_t lut[256];
    for (int v = 0; v < 256; v++) {
        int x = v + delta;
        if (x < 0) x = 0;
        if (x > 255) x = 255;
        lut[v] = (uint8_t)x;
    }
    int n = img->w * img->h;
    uint32_t *px = img->px;
    for (int i = 0; i < n; i++) {
        uint32_t c = px[i];
        px[i] = ((uint32_t)lut[(c >> 16) & 0xFF] << 16)
              | ((uint32_t)lut[(c >> 8) & 0xFF] << 8)
              |  (uint32_t)lut[c & 0xFF];
    }
}
