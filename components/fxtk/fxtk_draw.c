/**
 * fxtk_draw.c — 矢量绘制 + 渲染管线 (支持窗口自由缩放版)
 */
#include "fxtk.h"
#include "fxtk_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const fx_driver_t *s_drv;
static fx_color_t s_color = FX_WHITE;
static int16_t s_clip_x1 = 0, s_clip_y1 = 0, s_clip_x2 = 32767, s_clip_y2 = 32767;
static int s_ox = 0, s_oy = 0;
static int s_framing = 0;

/* 【核心修复】动态行缓冲：窗口超过 512 宽时自动扩容，不再溢出闪退 */
/* 行缓冲按"连续段"刷新: 一行内可能有多个不相邻像素(如圆的左右两点)。
 * 之前按 x0..x1 整段 push 会把其它行的旧像素一起带上, 造成圆/圆弧"实心/蝴蝶结"伪影。
 * 现在只在遇到相邻像素时扩展当前段, 遇断点立即刷出, 保证不带入脏像素。 */
static uint32_t *s_line = NULL;
static int s_line_cap = 0;
static int s_line_y = -1;
static int s_run_x0 = 0, s_run_x1 = 0, s_run_on = 0;

static uint32_t *s_offbuf_active = NULL;
static int s_offing = 0;
static int s_offw = 0, s_offh = 0;

int fx_band_index(void) { return -1; }

/* ================= 抗锯齿 (v2.2) =================
 * 用"到图形的距离"做边缘覆盖度, 在离屏帧缓冲(offbuf)上读回目标像素做 alpha 混合。
 * 只有离屏/帧缓冲路径可回读目标色; 非离屏(direct 行缓冲)时 AA 自动降级为普通绘制。
 * 全局开关: fx_set_aa(on)。v2.2 默认开启(可用宏 FX_AA_DEFAULT=0 或运行时 fx_set_aa(0) 关闭)。 */
#ifndef FX_AA_DEFAULT
#define FX_AA_DEFAULT 1
#endif
static int s_aa = FX_AA_DEFAULT;
int fxtk_aa(void) { return s_aa; }
void fx_set_aa(int on) { s_aa = on ? 1 : 0; }

/* 覆盖度混合: cov 0..255, 写回离屏缓冲 */
static inline void aa_blend(int x, int y, uint32_t c, int cov)
{
    if (!s_offing || !s_offbuf_active) return;
    if (x < 0 || y < 0 || x >= s_offw || y >= s_offh) return;
    uint32_t *d = &s_offbuf_active[y * s_offw + x];
    uint32_t ar = (c >> 16) & 0xFF, ag = (c >> 8) & 0xFF, ab = c & 0xFF;
    uint32_t dr = (*d >> 16) & 0xFF, dg = (*d >> 8) & 0xFF, db = *d & 0xFF;
    uint32_t r = (ar * cov + dr * (255 - cov)) / 255;
    uint32_t g = (ag * cov + dg * (255 - cov)) / 255;
    uint32_t b = (ab * cov + db * (255 - cov)) / 255;
    *d = 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* 抗锯齿线段: 到线段的距离决定覆盖度 (1px 半宽, 边缘平滑) */
static void aa_line(int x1, int y1, int x2, int y2, uint32_t c)
{
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len2 = dx * dx + dy * dy; if (len2 < 0.001f) len2 = 1;
    int minx = (x1 < x2 ? x1 : x2) - 1, maxx = (x1 > x2 ? x1 : x2) + 1;
    int miny = (y1 < y2 ? y1 : y2) - 1, maxy = (y1 > y2 ? y1 : y2) + 1;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float t = ((x - x1) * dx + (y - y1) * dy) / len2; if (t < 0) t = 0; if (t > 1) t = 1;
            float px = x1 + t * dx, py = y1 + t * dy;
            float dist = (float)sqrt((x - px) * (x - px) + (y - py) * (y - py));
            float cov = 0.5f + 0.5f - dist;          /* 半像素宽 */
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 抗锯齿圆描边: 到圆心距离接近半径处的 1px 圆环带, 边缘平滑 */
static void aa_circle(int cx, int cy, int r, uint32_t c)
{
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            float d = (float)sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float cov = 1.0f - (float)fabs(d - r);       /* 1px 圆环带, 中心 d=r 全覆盖 */
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 抗锯齿实心圆: 内部 d<r 全覆盖, 边缘 d≈r 处平滑淡出 */
static void aa_fill_circle(int cx, int cy, int r, uint32_t c)
{
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            float d = (float)sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float cov = (r + 0.5f) - d;
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 圆角矩形的有符号距离 (SDF): <0 内部, >0 外部 */
static float sd_round_box(float px, float py, float cx0, float cy0, float hx, float hy, float r)
{
    float qx = fabsf(px - cx0) - (hx - r);
    float qy = fabsf(py - cy0) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

/* 抗锯齿实心圆角矩形: 到圆角矩形边界的距离决定覆盖度, 角部平滑 */
static void aa_fill_rect_round(int x1, int y1, int x2, int y2, int r, uint32_t c)
{
    if (r < 0) r = 0;
    float cx0 = (x1 + x2) * 0.5f, cy0 = (y1 + y2) * 0.5f;
    float hx = (x2 - x1 + 1) * 0.5f, hy = (y2 - y1 + 1) * 0.5f;
    int pad = r + 1;
    for (int y = y1 - pad; y <= y2 + pad; y++)
        for (int x = x1 - pad; x <= x2 + pad; x++) {
            float d = sd_round_box((float)x, (float)y, cx0, cy0, hx, hy, (float)r);
            float cov = 0.5f - d;
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 抗锯齿圆弧: 到圆心距离接近半径的 1px 圆环带, 且角度落在 [a1,a2] 内, 边缘平滑 (连续无断点) */
static void aa_arc(int cx, int cy, int r, int a1, int a2, uint32_t c)
{
    if (r <= 0) return;
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    const float DEG = 57.2957795f;   /* 180/pi */
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            float d = (float)sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float cov = 1.0f - (float)fabs(d - r);
            if (cov <= 0) continue;
            float ang = atan2f((float)(y - cy), (float)(x - cx)) * DEG;   /* -180..180, 0=+x, +y往下 */
            if (ang < 0) ang += 360.0f;
            if (ang < (float)a1 || ang > (float)a2) continue;
            if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 椭圆边缘的有符号距离近似 SDF: |f-1|/|grad f| */
static void aa_ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
    if (rx <= 0 || ry <= 0) return;
    float frx = (float)rx, fry = (float)ry;
    for (int y = cy - ry - 1; y <= cy + ry + 1; y++)
        for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
            float dx = x - cx, dy = y - cy;
            float f = (dx * dx) / (frx * frx) + (dy * dy) / (fry * fry);
            float gx = 2.0f * dx / (frx * frx), gy = 2.0f * dy / (fry * fry);
            float gm = sqrtf(gx * gx + gy * gy) + 1e-5f;
            float dist = (f - 1.0f) / gm;              /* 到椭圆表面的近似距离 */
            float cov = 0.5f - fabsf(dist);            /* 1px 描边带 */
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 抗锯齿实心椭圆: 内部 f<1 全覆盖, 边缘平滑淡出 */
static void aa_fill_ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
    if (rx <= 0 || ry <= 0) return;
    float frx = (float)rx, fry = (float)ry;
    for (int y = cy - ry - 1; y <= cy + ry + 1; y++)
        for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
            float dx = x - cx, dy = y - cy;
            float f = (dx * dx) / (frx * frx) + (dy * dy) / (fry * fry);
            float gx = 2.0f * dx / (frx * frx), gy = 2.0f * dy / (fry * fry);
            float gm = sqrtf(gx * gx + gy * gy) + 1e-5f;
            float dist = (f - 1.0f) / gm;
            float cov = 0.5f - dist;
            if (cov <= 0) continue; if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}


static void line_ensure(int need)
{
    if (need < s_line_cap) return;
    int newcap = (need + 512) & ~511;
    uint32_t *nb = (uint32_t *)realloc(s_line, (size_t)newcap * sizeof(uint32_t));
    if (!nb) return;
    s_line = nb;
    s_line_cap = newcap;
}

static void flush_line(void)
{
    if (!s_run_on) return;
    s_drv->set_window((uint16_t)s_run_x0, (uint16_t)s_line_y,
                      (uint16_t)s_run_x1, (uint16_t)s_line_y);
    s_drv->push_pixels(&s_line[s_run_x0], (uint32_t)(s_run_x1 - s_run_x0 + 1));
    s_run_on = 0;
}

void fxtk_put_px(int x, int y, uint32_t c)
{
    if (x < s_clip_x1 || x > s_clip_x2 || y < s_clip_y1 || y > s_clip_y2) return;
    x += s_ox; y += s_oy;
    if (s_offing && s_offbuf_active) {
        if (x < 0 || y < 0 || x >= s_offw || y >= s_offh) return;
        s_offbuf_active[y * s_offw + x] = c;
        return;
    }
    if (x < 0 || y < 0 || x >= s_drv->width || y >= s_drv->height) return;
    if (y != s_line_y) {
        flush_line();
        s_line_y = y; s_run_x0 = s_run_x1 = x; s_run_on = 1;
    } else if (s_run_on && x == s_run_x1 + 1) {
        s_run_x1 = x;                                   /* 与当前段相邻, 并入 */
    } else {
        flush_line();                                   /* 断点: 先刷出上一段 */
        s_run_x0 = s_run_x1 = x; s_run_on = 1;
    }
    line_ensure(x);                 /* 【修复】按需扩容行缓冲 */
    if (x >= s_line_cap) return;    /* realloc 失败的保底保护 */
    s_line[x] = c;
}

void fxtk_draw_set_driver(const fx_driver_t *drv)
{
    s_drv = drv;
    line_ensure(drv->width + 1);    /* 【修复】预分配屏幕宽度 */
}

void fxtk_draw_flush_all(void) { flush_line(); }
void fx_frame_begin(void) { if (!s_drv) return; s_framing = 1; }
void fx_frame_end(void) { if (!s_drv) return; flush_line(); s_framing = 0; }
void fx_set_color(fx_color_t c) { s_color = c; }

void fx_set_clip(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    s_clip_x1 = (int16_t)x1; s_clip_y1 = (int16_t)y1;
    s_clip_x2 = (int16_t)x2; s_clip_y2 = (int16_t)y2;
}

void fx_reset_clip(void)
{
    s_clip_x1 = 0; s_clip_y1 = 0;
    s_clip_x2 = 32767; s_clip_y2 = 32767;
}

void fx_canvas_begin(fx_widget_t *cv)
{
    if (!cv) return;
    if (s_offing) {
        s_ox = 0; s_oy = 0;
        fx_set_clip(0, 0, s_offw - 1, s_offh - 1);
        return;
    }
    s_ox = cv->x1; s_oy = cv->y1;
    fx_set_clip(0, 0, cv->x2 - cv->x1, cv->y2 - cv->y1);
}

void fx_canvas_end(void)
{
    s_ox = 0; s_oy = 0;
    fx_reset_clip();
}

void fxtk_off_begin(fx_widget_t *cv)
{
    if (!cv->offbuf) return;
    int w = cv->x2 - cv->x1 + 1;
    int h = cv->y2 - cv->y1 + 1;
    if (w <= 0 || h <= 0) return;
    if ((int64_t)w * h > 4000000) { s_offing = 0; s_offbuf_active = NULL; return; }   /* 超大画布直绘, 防内存爆炸 */
    /* 【缩放保护】控件尺寸变化时重分配离屏缓冲，防止堆溢出 */
    if (w != cv->offw || h != cv->offh) {
        free(cv->offbuf);
        cv->offbuf = malloc((size_t)w * (size_t)h * 4);
        if (!cv->offbuf) {
            cv->offw = cv->offh = 0;
            cv->flags &= (uint8_t)~FX_F_BUF;
            return;
        }
        cv->offw = (uint16_t)w;
        cv->offh = (uint16_t)h;
    }
    s_offbuf_active = cv->offbuf;
    s_offing = 1;
    s_offw = w; s_offh = h;
    s_ox = -cv->x1; s_oy = -cv->y1;
    fx_set_clip(0, 0, s_offw - 1, s_offh - 1);
}

void fxtk_off_end(fx_widget_t *cv)
{
    if (!s_offbuf_active) return;
    s_drv->set_window((uint16_t)cv->x1, (uint16_t)cv->y1, (uint16_t)cv->x2, (uint16_t)cv->y2);
    s_drv->push_pixels(cv->offbuf, (uint32_t)(s_offw * s_offh));
    s_offbuf_active = NULL;
    s_offing = 0;
    s_ox = 0; s_oy = 0;
    fx_reset_clip();
}

int fx_canvas_enable_buf(fx_widget_t *cv)
{
    if (!cv || cv->type != FX_W_CANVAS) return -1;
    if (cv->offbuf) return 0;
    int w = cv->x2 - cv->x1 + 1, h = cv->y2 - cv->y1 + 1;
    if (w <= 0 || h <= 0) return -1;
    /* 超大画布拒绝离屏, 防内存爆炸 (与 fxtk_off_begin 的阈值一致) */
    if ((int64_t)w * h > 4000000) return -1;
    cv->offbuf = malloc((size_t)w * (size_t)h * 4);
    if (!cv->offbuf) return -1;
    cv->offw = (uint16_t)w;
    cv->offh = (uint16_t)h;
    cv->flags |= FX_F_BUF;   /* 任何画布都允许离屏缓冲 (v2.2 去掉按名字的性能锁) */
    return 0;
}

void fx_canvas_size(fx_widget_t *cv, int *w, int *h)
{
    if (!cv) { if (w) *w = 0; if (h) *h = 0; return; }
    int x1, y1, x2, y2;
    fx_widget_rect(cv, &x1, &y1, &x2, &y2);
    if (w) *w = x2 - x1 + 1;
    if (h) *h = y2 - y1 + 1;
}

/* 一键清空画布到指定颜色 (canvas 回调内用本地坐标 0,0..cw-1,ch-1) */
void fx_canvas_clear(fx_widget_t *cv, fx_color_t color)
{
    if (!cv || cv->type != FX_W_CANVAS) return;
    int cw, ch;
    fx_canvas_size(cv, &cw, &ch);
    if (cw <= 0 || ch <= 0) return;
    fx_set_color(color);
    fx_fill_rect(0, 0, cw - 1, ch - 1);
}

/* ================================================================
 * 基础图元
 * ================================================================ */
void fx_draw_pixel(int x, int y) { fxtk_put_px(x, y, s_color); }

void fx_draw_hline(int x1, int x2, int y)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x1 < s_clip_x1) x1 = s_clip_x1;
    if (x2 > s_clip_x2) x2 = s_clip_x2;
    if (y < s_clip_y1 || y > s_clip_y2 || x1 > x2) return;
    if (!s_offing && s_drv->fill_rect) {
        s_drv->fill_rect((uint16_t)(x1+s_ox),(uint16_t)(y+s_oy),(uint16_t)(x2+s_ox),(uint16_t)(y+s_oy), s_color);
        return;
    }
    for (int x = x1; x <= x2; x++) fxtk_put_px(x, y, s_color);
}

void fx_draw_vline(int x, int y1, int y2)
{
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y1 < s_clip_y1) y1 = s_clip_y1;
    if (y2 > s_clip_y2) y2 = s_clip_y2;
    if (x < s_clip_x1 || x > s_clip_x2 || y1 > y2) return;
    if (!s_offing && s_drv->fill_rect) {
        s_drv->fill_rect((uint16_t)(x+s_ox),(uint16_t)(y1+s_oy),(uint16_t)(x+s_ox),(uint16_t)(y2+s_oy), s_color);
        return;
    }
    for (int y = y1; y <= y2; y++) fxtk_put_px(x, y, s_color);
}

void fx_draw_line(int x1, int y1, int x2, int y2)
{
    if (s_aa && s_offing) { aa_line(x1, y1, x2, y2, s_color); return; }   /* v2.2 抗锯齿 */
    if (!s_offing && s_drv && s_drv->draw_line) {   /* v2: 折线GPU (去clip保批) */
        flush_line();
        s_drv->draw_line(x1+s_ox,y1+s_oy,x2+s_ox,y2+s_oy,s_color);
        return;
    }

    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        fxtk_put_px(x1, y1, s_color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

void fx_draw_rect(int x1, int y1, int x2, int y2)
{
    if (s_aa && s_offing) {   /* v2.2 抗锯齿: 四条边各用 AA 线段 */
        aa_line(x1, y1, x2, y1, s_color); aa_line(x1, y2, x2, y2, s_color);
        aa_line(x1, y1, x1, y2, s_color); aa_line(x2, y1, x2, y2, s_color);
        return;
    }
    fx_draw_hline(x1, x2, y1); fx_draw_hline(x1, x2, y2);
    fx_draw_vline(x1, y1, y2); fx_draw_vline(x2, y1, y2);
}

int fxtk_drv_width(void) { return s_drv->width; }
int fxtk_drv_height(void) { return s_drv->height; }
void fx_fill_rect(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < s_clip_x1) x1 = s_clip_x1;
    if (y1 < s_clip_y1) y1 = s_clip_y1;
    if (x2 > s_clip_x2) x2 = s_clip_x2;
    if (y2 > s_clip_y2) y2 = s_clip_y2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= s_drv->width) x2 = s_drv->width - 1;
    { int y2c = y2, hc = s_drv->height;
      if (y2c >= hc) { y2 = hc - 1; } }
    if (x1 > x2 || y1 > y2) return;
    if (s_offing && s_offbuf_active) {   /* v2.2 离屏快刷: 直接写 offbuf 行, 免去逐像素调用/边界检查 */
        int cx1 = x1 > 0 ? x1 : 0, cy1 = y1 > 0 ? y1 : 0;
        int cx2 = x2 < s_offw - 1 ? x2 : s_offw - 1, cy2 = y2 < s_offh - 1 ? y2 : s_offh - 1;
        if (cx1 <= cx2 && cy1 <= cy2) {
            for (int y = cy1; y <= cy2; y++) {
                uint32_t *row = &s_offbuf_active[(size_t)y * s_offw];
                for (int x = cx1; x <= cx2; x++) row[x] = s_color;
            }
            return;
        }
    }
    if (!s_offing && s_drv->fill_rect) {   /* v2: 帧内也走驱动几何批 */
        flush_line();
        int ax1 = x1 + s_ox, ay1 = y1 + s_oy, ax2 = x2 + s_ox, ay2 = y2 + s_oy;
        if (ax2 >= s_drv->width) ax2 = s_drv->width - 1;
        if (ay2 >= s_drv->height) ay2 = s_drv->height - 1;
        if (ax1 <= ax2 && ay1 <= ay2)
            s_drv->fill_rect((uint16_t)ax1, (uint16_t)ay1, (uint16_t)ax2, (uint16_t)ay2, s_color);
        return;
    }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            fxtk_put_px(x, y, s_color);
}

static int isqrt(int n)
{
    if (n <= 0) return 0;
    int lo = 0, hi = 65536;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= n) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static void round_cut(int y, int y1, int y2, int r, int *lcut, int *rcut)
{
    *lcut = 0; *rcut = 0;
    int dy;
    if (y - y1 < r) dy = r - (y - y1);
    else if (y2 - y < r) dy = r - (y2 - y);
    else return;
    *lcut = *rcut = r - isqrt(r * r - dy * dy);
}

/* 渐变填充: 从 c1 到 c2 线性过渡 (vertical=1 上下, 0 左右), 逐行/逐列画 */
void fx_fill_rect_gradient(int x1, int y1, int x2, int y2, fx_color_t c1, fx_color_t c2, int vertical)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    int len = vertical ? (y2 - y1 + 1) : (x2 - x1 + 1);
    if (len <= 0) return;
    int cr = (c1 >> 16) & 0xFF, cg = (c1 >> 8) & 0xFF, cb = c1 & 0xFF;
    int er = (c2 >> 16) & 0xFF, eg = (c2 >> 8) & 0xFF, eb = c2 & 0xFF;
    for (int i = 0; i < len; i++) {
        int t = i * 255 / len;
        int r = (cr * (255 - t) + er * t) / 255;
        int g = (cg * (255 - t) + eg * t) / 255;
        int b = (cb * (255 - t) + eb * t) / 255;
        fx_set_color(FX_RGB(r, g, b));
        if (vertical) fx_draw_hline(x1, x2, y1 + i);
        else          fx_draw_vline(x1 + i, y1, y2);
    }
}

void fx_fill_rect_round(int x1, int y1, int x2, int y2, int r)
{
    if (r <= 0) { fx_fill_rect(x1, y1, x2, y2); return; }
    if (s_aa && s_offing) { aa_fill_rect_round(x1, y1, x2, y2, r, s_color); return; }   /* v2.2 抗锯齿 */
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int y = y1; y <= y2; y++) {
        int lc, rc;
        round_cut(y, y1, y2, r, &lc, &rc);
        fx_draw_hline(x1 + lc, x2 - rc, y);
    }
}

void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r)
{
    if (r <= 0) { fx_draw_rect(x1, y1, x2, y2); return; }
    fx_draw_hline(x1 + r, x2 - r, y1);
    fx_draw_hline(x1 + r, x2 - r, y2);
    fx_draw_vline(x1, y1 + r, y2 - r);
    fx_draw_vline(x2, y1 + r, y2 - r);
    for (int a = 0; a <= 90; a += 3) {
        double rad = a * 3.14159265 / 180.0;
        int dx = (int)(r * cos(rad) + 0.5);
        int dy = (int)(r * sin(rad) + 0.5);
        fxtk_put_px(x1 + r - dx, y1 + r - dy, s_color);
        fxtk_put_px(x2 - r + dx, y1 + r - dy, s_color);
        fxtk_put_px(x1 + r - dx, y2 - r + dy, s_color);
        fxtk_put_px(x2 - r + dx, y2 - r + dy, s_color);
    }
}

void fx_draw_circle(int cx, int cy, int r)
{
    if (s_aa && s_offing) { aa_circle(cx, cy, r, s_color); return; }   /* v2.2 抗锯齿 */
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        fxtk_put_px(cx + x, cy + y, s_color);
        fxtk_put_px(cx - x, cy + y, s_color);
        fxtk_put_px(cx + x, cy - y, s_color);
        fxtk_put_px(cx - x, cy - y, s_color);
        fxtk_put_px(cx + y, cy + x, s_color);
        fxtk_put_px(cx - y, cy + x, s_color);
        fxtk_put_px(cx + y, cy - x, s_color);
        fxtk_put_px(cx - y, cy - x, s_color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

void fx_fill_circle(int cx, int cy, int r)
{
    if (r <= 0) return;
    if (s_aa && s_offing) { aa_fill_circle(cx, cy, r, s_color); return; }   /* v2.2 抗锯齿 */
    if (!s_offing && s_drv && s_drv->fill_tri) {   /* v2: 圆=GPU三角扇, 1次提交 */
        const int SEG = 14;
        for (int i = 0; i < SEG; i++) {
            double a1 = i*6.2831853/SEG, a2 = (i+1)*6.2831853/SEG;
            fx_fill_triangle(cx, cy,
                cx+(int)(r*cos(a1)+0.5), cy+(int)(r*sin(a1)+0.5),
                cx+(int)(r*cos(a2)+0.5), cy+(int)(r*sin(a2)+0.5));
        }
        return;
    }
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt(r * r - dy * dy);
        fx_draw_hline(cx - dx, cx + dx, cy + dy);
    }
}

void fx_draw_ellipse(int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) return;
    if (s_aa && s_offing) { aa_ellipse(cx, cy, rx, ry, s_color); return; }   /* v2.2 抗锯齿 */
    int x = -rx;
    while (x <= rx) {
        double t = 1.0 - (double)(x * x) / (double)(rx * rx);
        int y = (int)(ry * sqrt(t > 0 ? t : 0) + 0.5);
        fxtk_put_px(cx + x, cy + y, s_color);
        fxtk_put_px(cx + x, cy - y, s_color);
        x++;
    }
}

void fx_fill_ellipse(int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) return;
    if (s_aa && s_offing) { aa_fill_ellipse(cx, cy, rx, ry, s_color); return; }   /* v2.2 抗锯齿 */
    for (int dy = -ry; dy <= ry; dy++) {
        double t = 1.0 - (double)(dy * dy) / (double)(ry * ry);
        int dx = (int)(rx * sqrt(t > 0 ? t : 0) + 0.5);
        fx_draw_hline(cx - dx, cx + dx, cy + dy);
    }
}

void fx_draw_arc(int cx, int cy, int r, int a1, int a2)
{
    if (s_aa && s_offing) { aa_arc(cx, cy, r, a1, a2, s_color); return; }   /* v2.2 抗锯齿 */
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    double rd=a1*3.14159265/180.0;
    int px=cx+(int)(r*cos(rd)+0.5), py=cy+(int)(r*sin(rd)+0.5);
    for (int a = a1+1; a <= a2; a++) {   /* v2: 弧=GPU折线段 */
        double rad = a * 3.14159265 / 180.0;
        int nx=cx+(int)(r*cos(rad)+0.5), ny=cy+(int)(r*sin(rad)+0.5);
        fx_draw_line(px,py,nx,ny); px=nx; py=ny;
    }
}

void fx_fill_arc(int cx, int cy, int r, int a1, int a2)
{
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    for (int a = a1; a <= a2; a += 2) {
        double rad = a * 3.14159265 / 180.0;
        int ex = cx + (int)(r * cos(rad) + 0.5);
        int ey = cy + (int)(r * sin(rad) + 0.5);
        fx_draw_line(cx, cy, ex, ey);
    }
}

void fx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
    fx_draw_line(x1, y1, x2, y2);
    fx_draw_line(x2, y2, x3, y3);
    fx_draw_line(x3, y3, x1, y1);
}

void fx_fill_polygon(const int16_t *pts, int n)
{
    if (n < 3) return;
    if (!s_offing && s_drv && s_drv->fill_tri) { for (int i=1;i+1<n;i++) fx_fill_triangle(pts[0],pts[1],pts[i*2],pts[i*2+1],pts[(i+1)*2],pts[(i+1)*2+1]); return; }
    int ymin = 32767, ymax = -32768;
    for (int i = 0; i < n; i++) {
        if (pts[i * 2 + 1] < ymin) ymin = pts[i * 2 + 1];
        if (pts[i * 2 + 1] > ymax) ymax = pts[i * 2 + 1];
    }
    if (ymax < ymin) return;

    int max_xs = n + 2;
    if (max_xs < 64) max_xs = 64;
    int *xs = malloc(max_xs * sizeof(int));
    if (!xs) return;

    for (int y = ymin; y <= ymax; y++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int x1 = pts[i * 2], y1 = pts[i * 2 + 1];
            int x2 = pts[j * 2], y2 = pts[j * 2 + 1];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                int64_t x = (int64_t)x1 + (int64_t)(y - y1) * (x2 - x1) / (y2 - y1);
                if (m < max_xs) xs[m++] = (int)x;
            }
        }
        for (int i = 0; i < m - 1; i++)
            for (int j = 0; j < m - 1 - i; j++)
                if (xs[j] > xs[j + 1]) {
                    int t = xs[j]; xs[j] = xs[j + 1]; xs[j + 1] = t;
                }
        for (int i = 0; i + 1 < m; i += 2) fx_draw_hline(xs[i], xs[i + 1], y);
    }
    free(xs);
}

void fx_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
    if (!s_offing && s_drv && s_drv->fill_tri) {   /* v2: 三角GPU (去clip保批) */
        flush_line();
        s_drv->fill_tri(x1+s_ox,y1+s_oy,x2+s_ox,y2+s_oy,x3+s_ox,y3+s_oy,s_color);
        return;
    }

    int16_t pts[6] = { (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2, (int16_t)x3, (int16_t)y3 };
    fx_fill_polygon(pts, 3);
}

void fx_draw_polygon(const int16_t *pts, int n)
{
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        fx_draw_line(pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1]);
    }
}

/* ================= 图片核心 (缩放 blit, 遵守 clip/离屏) ================= */
fx_image_t *fx_image_create(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    fx_image_t *img = (fx_image_t *)malloc(sizeof(fx_image_t));
    if (!img) return NULL;
    img->px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!img->px) { free(img); return NULL; }
    memset(img->px, 0, (size_t)w * h * 4);
    img->w = (int16_t)w; img->h = (int16_t)h;
    return img;
}
void fx_image_free(fx_image_t *img)
{
    if (!img) return;
    free(img->px); free(img);
}
void fx_image_set_px(fx_image_t *img, int x, int y, fx_color_t c)
{
    if (!img || x < 0 || y < 0 || x >= img->w || y >= img->h) return;
    img->px[y * img->w + x] = c;
}
static uint32_t img_darken(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return (r / 2) << 16 | (g / 2) << 8 | (b / 2);
}
void fx_draw_image_ex(fx_image_t *img, int x, int y, int dw, int dh, int dark)
{
    if (!img || !img->px || dw <= 0 || dh <= 0) return;
    if (!s_offing && s_drv && s_drv->blit_img) {   /* v2: 图片上交 GPU 缩放 */
        flush_line();
        if (s_drv->set_clip_rect) s_drv->set_clip_rect(s_clip_x1+s_ox,s_clip_y1+s_oy,s_clip_x2+s_ox,s_clip_y2+s_oy);
        s_drv->blit_img(img->px,img->w,img->h,x+s_ox,y+s_oy,dw,dh,dark);
        if (s_drv->set_clip_rect) s_drv->set_clip_rect(0,0,32767,32767);
        return;
    }
    for (int j = 0; j < dh; j++) {
        int sy = (int)((int64_t)j * img->h / dh);
        if (sy >= img->h) sy = img->h - 1;
        const uint32_t *row = &img->px[sy * img->w];
        for (int i = 0; i < dw; i++) {
            int sx = (int)((int64_t)i * img->w / dw);
            if (sx >= img->w) sx = img->w - 1;
            uint32_t c = row[sx];
            fxtk_put_px(x + i, y + j, dark ? img_darken(c) : c);
        }
    }
}
void fx_draw_image(fx_image_t *img, int x, int y, int dw, int dh)
{
    fx_draw_image_ex(img, x, y, dw, dh, 0);
}
int fxtk_is_offing(void){ return s_offing; }
void fxtk_text_blit(void *tex,int x,int y,int w,int h)
{
    if(!s_drv||!s_drv->blit_tex)return;
    int x1=x>s_clip_x1?x:s_clip_x1;
    int y1=y>s_clip_y1?y:s_clip_y1;
    int x2=(x+w-1)<s_clip_x2?(x+w-1):s_clip_x2;
    int y2=(y+h-1)<s_clip_y2?(y+h-1):s_clip_y2;
    if(x1>x2||y1>y2)return;
    s_drv->blit_tex(tex,x1-x,y1-y,x2-x1+1,y2-y1+1,x1+s_ox,y1+s_oy);
}

int fxtk_image_rot_gpu(const fx_image_t *img,int cx,int cy,int dw,int dh,double ang)
{
    if(s_offing||!s_drv||!s_drv->blit_img_rot||!img||!img->px)return 0;
    if(s_drv->set_clip_rect) s_drv->set_clip_rect(s_clip_x1+s_ox,s_clip_y1+s_oy,s_clip_x2+s_ox,s_clip_y2+s_oy);   /* 画布裁剪 */
    s_drv->blit_img_rot(img->px,img->w,img->h,cx+s_ox,cy+s_oy,dw,dh,ang);
    if(s_drv->set_clip_rect) s_drv->set_clip_rect(0,0,32767,32767);
    return 1;
}

void fxtk_dbg_get_off(int*a,int*b){ *a=s_ox; *b=s_oy; }
