/**
 * fxtk_draw.c — 矢量绘制 + 渲染管线 (支持窗口自由缩放版)
 */
#include "fxtk.h"
#include "fxtk_internal.h"
#include "fxtk_backends.h"   /* v2.4: 统一日志出口 */
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
 * 全局开关: fx_set_aa(on)。默认关闭(opt-in), 避免把一切画布都改成离屏/CPU 渲染而影响按直接/GPU
 * 绘制设计的页面(如滚动页); 需要抗锯齿的地方调 fx_set_aa(1)。 */
#ifndef FX_AA_DEFAULT
#define FX_AA_DEFAULT 0
#endif
static int s_aa = FX_AA_DEFAULT;
int fxtk_aa(void) { return s_aa; }
/* v2.4: GPU 控件层抗锯齿档位。与画布 CPU AA(上面那个 s_aa)解耦 ——
 * 后者会把画布强制离屏并逐像素混合(开销大, 默认关), 前者是驱动的 SDF 钩子(默认开)。 */
static int s_widget_aa = 1;
int  fx_widget_aa_level(void) { return s_widget_aa; }
void fx_set_widget_aa(int level)
{
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (level == s_widget_aa) return;
    s_widget_aa = level;
    fx_repaint();
}
void fx_set_aa(int level)
{
    s_aa = level > 0 ? 1 : 0;                 /* 兼容 v2.2 语义: 非 0 即开画布 CPU AA */
    fx_set_widget_aa(level);
}
/* 供驱动在掉帧时调用: 降一档(0 档到底), 只打印一次, 并请求重绘让画面换到低档 */
int fx_aa_autodegrade(void)
{
    if (s_widget_aa <= 0) return 0;
    s_widget_aa--;
    fx_log(FX_LOG_WARN, "[aa] 帧率不足, 抗锯齿自动降档 -> 档位 %d", s_widget_aa);
    fx_repaint();
    return s_widget_aa;
}

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
    float len2 = dx * dx + dy * dy;
    if (len2 < 0.001f) len2 = 1;
    int minx = (x1 < x2 ? x1 : x2) - 1, maxx = (x1 > x2 ? x1 : x2) + 1;
    int miny = (y1 < y2 ? y1 : y2) - 1, maxy = (y1 > y2 ? y1 : y2) + 1;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float t = ((x - x1) * dx + (y - y1) * dy) / len2;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float px = x1 + t * dx, py = y1 + t * dy;
            float dist = (float)sqrt((x - px) * (x - px) + (y - py) * (y - py));
            float cov = 0.5f + 0.5f - dist;          /* 半像素宽 */
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 抗锯齿圆描边 (扫描线: 每行只扫圆环带 ~3px, 而非整框 O(r²) sqrt) */
static void aa_circle(int cx, int cy, int r, uint32_t c)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        float rad = (float)(r * r - dy * dy);
        if (rad < 0) continue;
        int gx = (int)sqrtf(rad);
        for (int dd = gx - 1; dd <= gx + 1; dd++) {
            float d = sqrtf((float)(dd * dd + dy * dy));
            float cov = 1.0f - (float)fabs(d - (float)r);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            aa_blend(cx - dd, y, c, (int)(cov * 255));
            aa_blend(cx + dd, y, c, (int)(cov * 255));
        }
    }
}

/* 抗锯齿实心圆: 内部 d<r 全覆盖, 边缘 d≈r 处平滑淡出 */
static void aa_fill_circle(int cx, int cy, int r, uint32_t c)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - dx, x1 = cx + dx;
        for (int x = x0 + 1; x < x1; x++) aa_blend(x, y, c, 255);   /* 内部全色 */
        float d0 = sqrtf((float)(dx * dx + dy * dy));
        float cov = (r + 0.5f) - d0;
        if (cov < 0) cov = 0;
        if (cov > 1) cov = 1;
        int ic = (int)(cov * 255);
        if (x0 == x1) aa_blend(x0, y, c, ic);   /* 顶部/底部只有 1 个像素 */
        else { aa_blend(x0, y, c, ic); aa_blend(x1, y, c, ic); }
    }
}

/* 圆角矩形的有符号距离 (SDF): <0 内部, >0 外部 */

/* 抗锯齿实心圆角矩形: 到圆角矩形边界的距离决定覆盖度, 角部平滑 */
static void aa_fill_rect_round(int x1, int y1, int x2, int y2, int r, uint32_t c)
{
    if (r < 0) r = 0;
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int y = y1; y <= y2; y++) {   /* 扫描线: 每行算圆角内缩, 填充内部+混边缘列 */
        int lc = 0, dy = 0;
        if (y - y1 < r) dy = r - (y - y1);
        else if (y2 - y < r) dy = r - (y2 - y);
        if (dy) lc = r - (int)sqrtf((float)(r * r - dy * dy));
        int lx = x1 + lc, rx2 = x2 - lc;
        for (int x = lx + 1; x < rx2; x++) aa_blend(x, y, c, 255);
        int ic = (lx >= rx2) ? 220 : 128;
        aa_blend(lx, y, c, ic);
        if (lx < rx2) aa_blend(rx2, y, c, 128);
    }
}

/* 抗锯齿圆弧: 到圆心距离接近半径的 1px 圆环带, 且角度落在 [a1,a2] 内, 边缘平滑 (连续无断点) */
static void aa_arc(int cx, int cy, int r, int a1, int a2, uint32_t c)
{
    if (r <= 0) return;
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    /* v2.3.1: 负角度区间 (仪表盘常用 -90..90) 归一化后按模长比较;
     * 旧逻辑 ang<a1||ang>a2 会把跨 0 段 (270..360) 整段丢掉 */
    int span = a2 - a1;
    if (span > 360) span = 360;
    a1 = (a1 % 360 + 360) % 360;
    const float DEG = 57.2957795f;   /* 180/pi */
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            float d = (float)sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float cov = 1.0f - (float)fabs(d - r);
            if (cov <= 0) continue;
            float ang = atan2f((float)(y - cy), (float)(x - cx)) * DEG;   /* -180..180, 0=+x, +y往下 */
            if (ang < 0) ang += 360.0f;
            int ai = (int)(ang + 0.5f);
            if (ai >= 360) ai = 0;
            int rel = ai - a1;
            if (rel < 0) rel += 360;
            if (rel > span) continue;
            if (cov > 1) cov = 1;
            aa_blend(x, y, c, (int)(cov * 255));
        }
}

/* 椭圆边缘的有符号距离近似 SDF: |f-1|/|grad f| */
static void aa_ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
    if (rx <= 0 || ry <= 0) return;
    float frx = (float)rx, fry = (float)ry;
    for (int dy = -ry; dy <= ry; dy++) {
        int y = cy + dy;
        float t = 1.0f - (float)(dy * dy) / (fry * fry);
        if (t < 0) t = 0;
        int gx = (int)(frx * sqrtf(t));
        for (int dd = gx - 1; dd <= gx + 1; dd++) {   /* 只扫椭圆边缘带 ~3px */
            float f = (float)(dd * dd) / (frx * frx) + (float)(dy * dy) / (fry * fry);
            float gxm = 2.0f * dd / (frx * frx), gym = 2.0f * dy / (fry * fry);
            float gm = sqrtf(gxm * gxm + gym * gym) + 1e-5f;
            float dist = (f - 1.0f) / gm;
            float cov = 0.5f - fabsf(dist);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            aa_blend(cx - dd, y, c, (int)(cov * 255));
            aa_blend(cx + dd, y, c, (int)(cov * 255));
        }
    }
}

/* 抗锯齿实心椭圆 (扫描线: 每行填充内部 + 只混 2 个边缘列) */
static void aa_fill_ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
    if (rx <= 0 || ry <= 0) return;
    float frx = (float)rx, fry = (float)ry;
    for (int dy = -ry; dy <= ry; dy++) {
        int y = cy + dy;
        float t = 1.0f - (float)(dy * dy) / (fry * fry);
        if (t < 0) t = 0;
        int dx = (int)(frx * sqrtf(t));
        int x0 = cx - dx, x1 = cx + dx;
        for (int x = x0 + 1; x < x1; x++) aa_blend(x, y, c, 255);
        /* 边缘列: 近似 50% 覆盖 */
        int ic = (x0 == x1) ? 200 : 128;
        aa_blend(x0, y, c, ic);
        if (x0 != x1) aa_blend(x1, y, c, 128);
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
    if (!s_run_on || !s_drv) return;
    s_drv->set_window((uint16_t)s_run_x0, (uint16_t)s_line_y,
                      (uint16_t)s_run_x1, (uint16_t)s_line_y);
    s_drv->push_pixels(&s_line[s_run_x0], (uint32_t)(s_run_x1 - s_run_x0 + 1));
    s_run_on = 0;
}

/* v2.3.1: GPU 快路径也必须遵守裁剪 —— 原先 draw_line/fill_tri 完全绕过 clip
 * (注释"去clip保批"), 在 tab/滚动容器/画布上下文里线条会溢出到邻居控件,
 * 而软件路径是裁剪的。参照 fx_draw_image_ex 的 set_clip_rect 包裹法。 */
static int gpu_clip_push(void)
{
    if (s_offing || !s_drv || !s_drv->set_clip_rect) return 0;
    if (s_clip_x1 <= 0 && s_clip_y1 <= 0 && s_clip_x2 >= 32767 && s_clip_y2 >= 32767) return 0;
    s_drv->set_clip_rect(s_clip_x1 + s_ox, s_clip_y1 + s_oy, s_clip_x2 + s_ox, s_clip_y2 + s_oy);
    return 1;
}
static void gpu_clip_pop(int pushed)
{
    if (pushed && s_drv && s_drv->set_clip_rect) s_drv->set_clip_rect(0, 0, 32767, 32767);
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
    line_ensure(x);                 /* 【修复】按需扩容行缓冲 */
    if (x >= s_line_cap) return;    /* v2.3.1: realloc 失败直接丢弃, 不推进 run (防 flush 带出未写入像素) */
    if (y != s_line_y) {
        flush_line();
        s_line_y = y; s_run_x0 = s_run_x1 = x; s_run_on = 1;
    } else if (s_run_on && x == s_run_x1 + 1) {
        s_run_x1 = x;                                   /* 与当前段相邻, 并入 */
    } else {
        flush_line();                                   /* 断点: 先刷出上一段 */
        s_run_x0 = s_run_x1 = x; s_run_on = 1;
    }
    s_line[x] = c;
}

void fxtk_draw_set_driver(const fx_driver_t *drv)
{
    s_drv = drv;
    line_ensure(drv->width + 1);    /* 【修复】预分配屏幕宽度 */
}

void fxtk_draw_flush_all(void) { flush_line(); }
void fx_frame_begin(void) { if (!s_drv) return; s_framing = 1; fx_transform_reset(); }
void fx_frame_end(void) { if (!s_drv) return; flush_line(); s_framing = 0; }
void fx_set_color(fx_color_t c) { s_color = c; }

void fx_set_clip(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < -32768) x1 = -32768; if (x2 > 32767) x2 = 32767;   /* v2.3.1: 防 int16 截断回绕 */
    if (y1 < -32768) y1 = -32768; if (y2 > 32767) y2 = 32767;
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
    flush_line();   /* v2.3.1: 先刷掉上个目标的滞留行, 防止之后被 blit 盖到新画布上 (z-order) */
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
    if (!s_offbuf_active || !s_drv) return;
    flush_line();   /* v2.3.1: blit 前先刷滞留行, 否则之后 flush 会把旧像素盖在画布上 (z-order 违例) */
    int sx1 = cv->x1, sy1 = cv->y1, sx2 = cv->x2, sy2 = cv->y2;
    /* v2.3.1: 与屏幕求交。旧代码直接 (uint16_t) 强转负坐标 → 回绕 65536 一侧, 整块内容丢失 */
    int cut_l = sx1 < 0 ? -sx1 : 0;   /* 源缓冲左侧被裁列数 */
    int cut_t = sy1 < 0 ? -sy1 : 0;
    int dw = s_offw - cut_l, dh = s_offh - cut_t;
    if (sx2 >= s_drv->width)  dw -= (sx2 - s_drv->width + 1);
    if (sy2 >= s_drv->height) dh -= (sy2 - s_drv->height + 1);
    int dx = sx1 + cut_l, dy = sy1 + cut_t;
    if (dw > 0 && dh > 0 && dx < s_drv->width && dy < s_drv->height) {
        if (cut_l == 0 && cut_t == 0 && sx2 < s_drv->width && sy2 < s_drv->height) {
            /* 完全在屏内: 原快路径整块提交 */
            s_drv->set_window((uint16_t)sx1, (uint16_t)sy1, (uint16_t)sx2, (uint16_t)sy2);
            s_drv->push_pixels(cv->offbuf, (uint32_t)(s_offw * s_offh));
        } else {
            /* 部分越屏: 逐行提交可见部分 */
            for (int r = 0; r < dh; r++) {
                s_drv->set_window((uint16_t)dx, (uint16_t)(dy + r),
                                  (uint16_t)(dx + dw - 1), (uint16_t)(dy + r));
                s_drv->push_pixels(cv->offbuf + (size_t)(cut_t + r) * s_offw + cut_l, (uint32_t)dw);
            }
        }
    }
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
    if (!cv) { if (w) *w = 0;
    if (h) *h = 0; return; }
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

/* ================= v2.4 canvas 变换栈 (2D 仿射) =================
 * 用途: 伪 3D 场景里"按平面摆放"——旋转/缩放/平铺/斜切墙地。
 * 只影响立即模式图元: 像素 / 线段 / 矩形填充(→实心四边形) / 图片(→透视四边形)。
 * fx_draw_hline/vline 是内部构件(小控件也用), 不参与变换, 以免二重变换。
 * 栈深 8, 栈满覆盖栈顶(不崩); 每帧开始自动复位。
 */
#define FX_XF_MAX 8
static float s_xf[FX_XF_MAX][6];
static int   s_xf_n = 0;
static int   s_xf_active = 0;

void fx_transform_reset(void) { s_xf_n = 0; s_xf_active = 0; }
int  fx_transform_depth(void) { return s_xf_n; }

void fx_canvas_push_affine(float a, float b, float c, float d, float e, float f)
{
    float m[6] = { a, b, c, d, e, f };
    if (s_xf_n > 0) {                     /* 与当前栈顶复合: 新变换后应用 */
        const float *p = s_xf[s_xf_n - 1];
        float r[6];
        r[0] = a * p[0] + c * p[1];
        r[1] = b * p[0] + d * p[1];
        r[2] = a * p[2] + c * p[3];
        r[3] = b * p[2] + d * p[3];
        r[4] = a * p[4] + c * p[5] + e;
        r[5] = b * p[4] + d * p[5] + f;
        memcpy(m, r, sizeof m);
    }
    if (s_xf_n < FX_XF_MAX) memcpy(s_xf[s_xf_n++], m, sizeof m);
    else memcpy(s_xf[FX_XF_MAX - 1], m, sizeof m);
    s_xf_active = 1;
}

void fx_canvas_pop_affine(void)
{
    if (s_xf_n > 0) s_xf_n--;
    s_xf_active = (s_xf_n > 0);
}

void fx_canvas_transform_point(float x, float y, float *ox, float *oy)
{
    if (!s_xf_active) { if (ox) *ox = x; if (oy) *oy = y; return; }
    const float *m = s_xf[s_xf_n - 1];
    if (ox) *ox = m[0] * x + m[2] * y + m[4];
    if (oy) *oy = m[1] * x + m[3] * y + m[5];
}

/* 矩形 → 变换后的四角 (顺时针, 右/下边界 +1 保持像素覆盖) */
static void xf_rect_corners(int x1, int y1, int x2, int y2, float *xy8)
{
    float ax, ay;
    fx_canvas_transform_point((float)x1, (float)y1, &ax, &ay);             xy8[0] = ax; xy8[1] = ay;
    fx_canvas_transform_point((float)(x2 + 1), (float)y1, &ax, &ay);       xy8[2] = ax; xy8[3] = ay;
    fx_canvas_transform_point((float)(x2 + 1), (float)(y2 + 1), &ax, &ay); xy8[4] = ax; xy8[5] = ay;
    fx_canvas_transform_point((float)x1, (float)(y2 + 1), &ax, &ay);       xy8[6] = ax; xy8[7] = ay;
}

/* ================================================================
 * 基础图元
 * ================================================================ */
void fx_draw_pixel(int x, int y)
{
    if (s_xf_active) {   /* v2.4: 变换栈 */
        float ox, oy;
        fx_canvas_transform_point((float)x, (float)y, &ox, &oy);
        fxtk_put_px((int)lroundf(ox), (int)lroundf(oy), s_color);
        return;
    }
    fxtk_put_px(x, y, s_color);
}

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

static void fx_draw_line_plain(int x1, int y1, int x2, int y2);

void fx_draw_line(int x1, int y1, int x2, int y2)
{
    if (s_xf_active) {   /* v2.4: 变换端点后走原路径 */
        float ax, ay, bx, by;
        fx_canvas_transform_point((float)x1, (float)y1, &ax, &ay);
        fx_canvas_transform_point((float)x2, (float)y2, &bx, &by);
        fx_draw_line_plain((int)lroundf(ax), (int)lroundf(ay), (int)lroundf(bx), (int)lroundf(by));
        return;
    }
    fx_draw_line_plain(x1, y1, x2, y2);
}

static void fx_draw_line_plain(int x1, int y1, int x2, int y2)
{
    if (s_aa && s_offing) { aa_line(x1, y1, x2, y2, s_color); return; }   /* v2.2 抗锯齿 */
    /* v2.4 档位 2: GPU 羽化线段(片元按到线心距离混合) —— 斜线不再有阶梯; 无钩子自动跳过 */
    if (!s_offing && !s_xf_active && s_widget_aa >= 2 && s_drv && s_drv->draw_line_aa) {
        flush_line();
        int pushed = gpu_clip_push();
        s_drv->draw_line_aa(x1 + s_ox, y1 + s_oy, x2 + s_ox, y2 + s_oy, 1, s_color);
        gpu_clip_pop(pushed);
        return;
    }
    if (!s_offing && s_drv && s_drv->draw_line) {   /* v2: 折线GPU; v2.3.1 补上 clip */
        flush_line();
        int pushed = gpu_clip_push();
        s_drv->draw_line(x1+s_ox,y1+s_oy,x2+s_ox,y2+s_oy,s_color);
        gpu_clip_pop(pushed);
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
    if (s_xf_active) {   /* v2.4: 变换生效 → 变实心四边形填充 (旋转/斜切矩形) */
        float xy8[8];
        xf_rect_corners(x1, y1, x2, y2, xy8);
        fx_fill_quad(xy8);
        return;
    }
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
    int denom = len > 1 ? len - 1 : 1;   /* v2.3.1: 旧 t=i*255/len 末行永远到不了 c2 */
    int cr = (c1 >> 16) & 0xFF, cg = (c1 >> 8) & 0xFF, cb = c1 & 0xFF;
    int er = (c2 >> 16) & 0xFF, eg = (c2 >> 8) & 0xFF, eb = c2 & 0xFF;
    for (int i = 0; i < len; i++) {
        int t = i * 255 / denom;
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
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    /* v2.4: 有 GPU SDF 钩子时一次 draw 完成 —— 边缘由片元按距离羽化(天然抗锯齿),
     * 比下面逐行填充更快也更平滑。离屏画布/变换栈上下文仍走原路径(那里坐标语义不同)。 */
    if (!s_offing && !s_xf_active && s_widget_aa >= 1 && s_drv && s_drv->fill_rect_round) {
        flush_line();
        int pushed = gpu_clip_push();
        s_drv->fill_rect_round(x1 + s_ox, y1 + s_oy, x2 + s_ox, y2 + s_oy, r, s_color);
        gpu_clip_pop(pushed);
        return;
    }
    if (s_aa && s_offing) { aa_fill_rect_round(x1, y1, x2, y2, r, s_color); return; }   /* v2.2 抗锯齿 */
    /* v2.3 性能: 中间带 [y1+r, y2-r] 各行裁剪量恒为 0 → 合并成一次填充。
     * 旧实现整块逐行 fx_draw_hline, 一个 20x16 圆角按钮要 16 次驱动矩形调用
     * (2816 控件压测实测该函数占 40% 自身耗时)。像素结果与逐行版本完全一致。 */
    int my1 = y1 + r, my2 = y2 - r;
    if (my1 <= my2) fx_fill_rect(x1, my1, x2, my2);
    for (int y = y1; y < my1; y++) {
        int lc, rc;
        round_cut(y, y1, y2, r, &lc, &rc);
        fx_draw_hline(x1 + lc, x2 - rc, y);
    }
    for (int y = my2 + 1; y <= y2; y++) {
        int lc, rc;
        round_cut(y, y1, y2, r, &lc, &rc);
        fx_draw_hline(x1 + lc, x2 - rc, y);
    }
}

void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r)
{
    if (r <= 0) { fx_draw_rect(x1, y1, x2, y2); return; }
    if (!s_offing && !s_xf_active && s_widget_aa >= 1 && s_drv && s_drv->stroke_rect_round) {
        flush_line();
        int pushed = gpu_clip_push();
        s_drv->stroke_rect_round(x1 + s_ox, y1 + s_oy, x2 + s_ox, y2 + s_oy, r, 1, s_color);
        gpu_clip_pop(pushed);
        return;
    }
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
    if (!s_offing && s_drv && s_drv->fill_tri) {   /* v2: 三角GPU; v2.3.1 补上 clip */
        flush_line();
        int pushed = gpu_clip_push();
        s_drv->fill_tri(x1+s_ox,y1+s_oy,x2+s_ox,y2+s_oy,x3+s_ox,y3+s_oy,s_color);
        gpu_clip_pop(pushed);
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
    if (w > 32767 || h > 32767) return NULL;   /* v2.3.1: w/h 存 int16_t, 越界截断成负数会让 blit 越界读 (P0) */
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
    if (s_xf_active) {   /* v2.4: 变换生效 → 走透视四边形 (旋转/缩放/斜切一次到位) */
        (void)dark;
        float xy8[8];
        xf_rect_corners(x, y, x + dw - 1, y + dh - 1, xy8);
        fx_draw_image_quad(img, xy8);
        return;
    }
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

/* v2.4: 图片解码迁到 backends(stb) —— 不再依赖 SDL_image, 且 PNG/JPG/BMP/GIF/TGA 全支持,
 * ESP32/无头环境同样可用(无 SDL 也能加载图片)。 */
fx_image_t *fx_image_load(const char *path)
{
    if (!path) return NULL;
    fx_img_t im;
    if (!fx_img_load_file(path, &im)) return NULL;
    fx_image_t *out = fx_image_create(im.w, im.h);
    if (out) {
        int n = im.w * im.h;
        for (int i = 0; i < n; i++) {
            const unsigned char *p = &im.pixels[(size_t)i * im.channels];
            uint32_t c;
            if (im.channels >= 3) c = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            else                  c = ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | (uint32_t)p[0];
            out->px[i] = c;
        }
    }
    fx_img_free(&im);
    return out;
}

/* ================= v2.4 GPU 光线步进入口 ================= */
int fx_quad_warp_gpu(void) { return (s_drv && s_drv->draw_image_quad) ? 1 : 0; }

int fx_raymarch_available(void) { return (s_drv && s_drv->raymarch) ? 1 : 0; }

void fx_draw_raymarch(float time, int x, int y, int w, int h)
{
    if (!s_drv || !s_drv->raymarch || w <= 0 || h <= 0) return;
    int x1 = x, y1 = y, x2 = x + w - 1, y2 = y + h - 1;
    if (x2 < s_clip_x1 || x1 > s_clip_x2 || y2 < s_clip_y1 || y1 > s_clip_y2) return;   /* 完全在裁剪外 */
    flush_line();
    s_drv->raymarch(time, x1 + s_ox, y1 + s_oy, x2 + s_ox, y2 + s_oy);
}

/* ================= v2.4 四边形形变 (projective quad warp) =================
 * 把整张图映射到任意凸四边形 —— 真透视, 不是两个三角形的仿射近似(那种画法有对角缝)。
 * 数学: 解 8 元线性方程组得单应矩阵 H (源单位方 → 目标四角), 渲染时用 H⁻¹ 逐像素
 * 反查源坐标并做双线性采样; "uv 是否落在 [0,1]" 即"是否在凸四边形内"。
 * 无 GPU 平台(ESP32)走这条 CPU 路径; 有 GPU 时由驱动的 draw_image_quad 钩子接管(P4)。
 */

/* 解 A·x = b (n×n 高斯消元, 双精度), 成功 1 */
static int solve_linear(double *A, double *b, int n, double *x)
{
    for (int c = 0; c < n; c++) {
        int piv = c;
        double best = fabs(A[c * n + c]);
        for (int r = c + 1; r < n; r++) {
            double v = fabs(A[r * n + c]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) return 0;
        if (piv != c)
            for (int k = 0; k < n; k++) {
                double t = A[c * n + k]; A[c * n + k] = A[piv * n + k]; A[piv * n + k] = t;
            }
        { double t = b[c]; b[c] = b[piv]; b[piv] = t; }
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            double f = A[r * n + c] / A[c * n + c];
            if (f == 0.0) continue;
            for (int k = c; k < n; k++) A[r * n + k] -= f * A[c * n + k];
            b[r] -= f * b[c];
        }
    }
    for (int i = 0; i < n; i++) x[i] = b[i] / A[i * n + i];
    return 1;
}

int fx_quad_homography(const float *src8, const float *dst8, float m[9])
{
    if (!src8 || !dst8 || !m) return 0;
    double A[64], b[8], x[8];
    for (int i = 0; i < 4; i++) {
        double sx = src8[i * 2], sy = src8[i * 2 + 1];
        double dx = dst8[i * 2], dy = dst8[i * 2 + 1];
        double *r0 = &A[(i * 2) * 8], *r1 = &A[(i * 2 + 1) * 8];
        r0[0] = sx; r0[1] = sy; r0[2] = 1; r0[3] = 0;  r0[4] = 0;  r0[5] = 0; r0[6] = -sx * dx; r0[7] = -sy * dx;
        r1[0] = 0;  r1[1] = 0;  r1[2] = 0; r1[3] = sx; r1[4] = sy; r1[5] = 1; r1[6] = -sx * dy; r1[7] = -sy * dy;
        b[i * 2] = dx; b[i * 2 + 1] = dy;
    }
    if (!solve_linear(A, b, 8, x)) return 0;
    for (int i = 0; i < 8; i++) m[i] = (float)x[i];
    m[8] = 1.0f;
    return 1;
}

int fx_quad_corner_weights(const float *xy8, float d4[4])
{
    static const float unit8[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    float H[9];
    if (!xy8 || !d4) return 0;
    if (!fx_quad_homography(unit8, xy8, H)) return 0;
    float g = H[6], h = H[7];
    float d[4];
    d[0] = 1.0f; d[1] = g + 1.0f; d[2] = g + h + 1.0f; d[3] = h + 1.0f;
    for (int i = 0; i < 4; i++) {
        if (fabs(d[i]) < 1e-6f) return 0;         /* 灭点落在角点 → 权重发散, 交回 CPU 路径 */
        if (d[i] < 0) d[i] = -d[i];               /* 只取比例, 符号无关(整幅同号) */
    }
    for (int i = 0; i < 4; i++) d4[i] = d[i];
    return 1;
}

int fx_mat3_invert(const float *m, float out[9])
{
    if (!m || !out) return 0;
    double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    double A =  (e * i - f * h), B = -(d * i - f * g), C =  (d * h - e * g);
    double det = a * A + b * B + c * C;
    if (fabs(det) < 1e-12) return 0;
    double id = 1.0 / det;
    out[0] = (float)(A * id);
    out[1] = (float)(-(b * i - c * h) * id);
    out[2] = (float)( (b * f - c * e) * id);
    out[3] = (float)(B * id);
    out[4] = (float)( (a * i - c * g) * id);
    out[5] = (float)(-(a * f - c * d) * id);
    out[6] = (float)(C * id);
    out[7] = (float)(-(a * h - b * g) * id);
    out[8] = (float)( (a * e - b * d) * id);
    return 1;
}

/* 双线性采样 (uv 已在 [0,1]) */
static uint32_t sample_bilinear(const fx_image_t *img, float u, float v)
{
    float fx = u * (float)(img->w - 1), fy = v * (float)(img->h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > img->w - 1) x0 = img->w - 1;
    if (y0 > img->h - 1) y0 = img->h - 1;
    int x1 = x0 + 1 < img->w ? x0 + 1 : x0;
    int y1 = y0 + 1 < img->h ? y0 + 1 : y0;
    float tx = fx - (float)x0, ty = fy - (float)y0;
    if (tx < 0) tx = 0; if (tx > 1) tx = 1;
    if (ty < 0) ty = 0; if (ty > 1) ty = 1;
    uint32_t c00 = img->px[y0 * img->w + x0], c10 = img->px[y0 * img->w + x1];
    uint32_t c01 = img->px[y1 * img->w + x0], c11 = img->px[y1 * img->w + x1];
    float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
    int r = (int)(((c00 >> 16 & 255) * w00 + (c10 >> 16 & 255) * w10 + (c01 >> 16 & 255) * w01 + (c11 >> 16 & 255) * w11) + 0.5f);
    int g = (int)(((c00 >> 8 & 255) * w00 + (c10 >> 8 & 255) * w10 + (c01 >> 8 & 255) * w01 + (c11 >> 8 & 255) * w11) + 0.5f);
    int b = (int)(((c00 & 255) * w00 + (c10 & 255) * w10 + (c01 & 255) * w01 + (c11 & 255) * w11) + 0.5f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* 四边形 → 包围盒(clip 后) + 逆单应; 退化返回 0 */
static int quad_setup(const float *xy8, int *bx1, int *by1, int *bx2, int *by2, float Hi[9])
{
    static const float unit8[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    float H[9];
    if (!fx_quad_homography(unit8, xy8, H)) return 0;
    if (!fx_mat3_invert(H, Hi)) return 0;
    float minx = xy8[0], maxx = xy8[0], miny = xy8[1], maxy = xy8[1];
    for (int i = 1; i < 4; i++) {
        if (xy8[i * 2] < minx) minx = xy8[i * 2];
        if (xy8[i * 2] > maxx) maxx = xy8[i * 2];
        if (xy8[i * 2 + 1] < miny) miny = xy8[i * 2 + 1];
        if (xy8[i * 2 + 1] > maxy) maxy = xy8[i * 2 + 1];
    }
    int x1 = (int)floorf(minx), y1 = (int)floorf(miny);
    int x2 = (int)ceilf(maxx), y2 = (int)ceilf(maxy);
    if (x1 < s_clip_x1) x1 = s_clip_x1;
    if (y1 < s_clip_y1) y1 = s_clip_y1;
    if (x2 > s_clip_x2) x2 = s_clip_x2;
    if (y2 > s_clip_y2) y2 = s_clip_y2;
    if (x1 > x2 || y1 > y2) return 0;
    *bx1 = x1; *by1 = y1; *bx2 = x2; *by2 = y2;
    return 1;
}

/* 实心四边形填充 (自有图元: 墙/地/任意四边形色块) */
void fx_fill_quad(const float *xy8)
{
    if (!xy8) return;
    int x1, y1, x2, y2; float Hi[9];
    if (!quad_setup(xy8, &x1, &y1, &x2, &y2, Hi)) return;
    uint32_t col = s_color;
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float wq = Hi[6] * px + Hi[7] * py + Hi[8];
            if (fabsf(wq) < 1e-9f) continue;
            float u = (Hi[0] * px + Hi[1] * py + Hi[2]) / wq;
            float v = (Hi[3] * px + Hi[4] * py + Hi[5]) / wq;
            /* v2.4: 边界留 1e-3 容差 —— 严格 [0,1] 判定会让相邻四边形之间漏出 1px 缝
             * (平铺贴图/瓷砖的"散架"观感即由此而来) */
            if (u < -1e-3f || u > 1.0f + 1e-3f || v < -1e-3f || v > 1.0f + 1e-3f) continue;
            fxtk_put_px(x, y, col);
        }
    }
}

/* 退化四边形回退: 用包围盒矩形映射 (并只告警一次, 不刷屏) */
static void quad_fallback(const fx_image_t *img, const float *xy8)
{
    static int warned = 0;
    if (!warned) { fx_log(FX_LOG_WARN, "fx_draw_image_quad: 四边形退化(面积≈0/自交?), 回退为包围盒映射"); warned = 1; }
    float minx = xy8[0], maxx = xy8[0], miny = xy8[1], maxy = xy8[1];
    for (int i = 1; i < 4; i++) {
        if (xy8[i * 2] < minx) minx = xy8[i * 2];
        if (xy8[i * 2] > maxx) maxx = xy8[i * 2];
        if (xy8[i * 2 + 1] < miny) miny = xy8[i * 2 + 1];
        if (xy8[i * 2 + 1] > maxy) maxy = xy8[i * 2 + 1];
    }
    int w = (int)(maxx - minx + 1), h = (int)(maxy - miny + 1);
    if (w > 0 && h > 0) fx_draw_image_ex((fx_image_t *)img, (int)minx, (int)miny, w, h, 0);
}

void fx_draw_image_quad(const fx_image_t *img, const float *xy8)
{
    if (!img || !img->px || !xy8 || img->w <= 0 || img->h <= 0) return;

    /* GPU 路径 (P4: sokol 顶点着色器做透视校正插值) */
    if (!s_offing && s_drv && s_drv->draw_image_quad) {
        flush_line();
        if (s_drv->set_clip_rect) s_drv->set_clip_rect(s_clip_x1 + s_ox, s_clip_y1 + s_oy, s_clip_x2 + s_ox, s_clip_y2 + s_oy);
        /* 【v2.4 修正】四角坐标必须同样加上画布原点偏移。CPU 路径是在离屏缓冲里按局部坐标画的、
         * 再由框架整体 blit, 所以不需要偏移; 而 GPU 钩子是直接画到【屏幕坐标系】的批次里 ——
         * 上面裁剪加了 s_ox/s_oy 而这里没加, 结果整幅图会偏移一个画布原点的量(实测偏了 (41,152))。 */
        float q8[8];
        for (int i = 0; i < 4; i++) { q8[i * 2] = xy8[i * 2] + s_ox; q8[i * 2 + 1] = xy8[i * 2 + 1] + s_oy; }
        s_drv->draw_image_quad(img->px, img->w, img->h, q8, 1);
        if (s_drv->set_clip_rect) s_drv->set_clip_rect(0, 0, 32767, 32767);
        return;
    }

    /* CPU 路径: H = 单位方 → 目标四角; 渲染用 H⁻¹ */
    static const float unit8[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    float H[9], Hi[9];
    if (!fx_quad_homography(unit8, xy8, H) || !fx_mat3_invert(H, Hi)) { quad_fallback(img, xy8); return; }

    float minx = xy8[0], maxx = xy8[0], miny = xy8[1], maxy = xy8[1];
    for (int i = 1; i < 4; i++) {
        if (xy8[i * 2] < minx) minx = xy8[i * 2];
        if (xy8[i * 2] > maxx) maxx = xy8[i * 2];
        if (xy8[i * 2 + 1] < miny) miny = xy8[i * 2 + 1];
        if (xy8[i * 2 + 1] > maxy) maxy = xy8[i * 2 + 1];
    }
    int bx1 = (int)floorf(minx), by1 = (int)floorf(miny);
    int bx2 = (int)ceilf(maxx), by2 = (int)ceilf(maxy);
    if (bx1 < s_clip_x1) bx1 = s_clip_x1;
    if (by1 < s_clip_y1) by1 = s_clip_y1;
    if (bx2 > s_clip_x2) bx2 = s_clip_x2;
    if (by2 > s_clip_y2) by2 = s_clip_y2;
    if (bx1 > bx2 || by1 > by2) return;

    for (int y = by1; y <= by2; y++) {
        for (int x = bx1; x <= bx2; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float wq = Hi[6] * px + Hi[7] * py + Hi[8];
            if (fabsf(wq) < 1e-9f) continue;
            float u = (Hi[0] * px + Hi[1] * py + Hi[2]) / wq;
            float v = (Hi[3] * px + Hi[4] * py + Hi[5]) / wq;
            /* 凸四边形内外判定 (边界留容差, 见 fx_fill_quad 的说明) */
            if (u < -1e-3f || u > 1.0f + 1e-3f || v < -1e-3f || v > 1.0f + 1e-3f) continue;
            if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
            if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
            fxtk_put_px(x, y, sample_bilinear(img, u, v));
        }
    }
}

void fx_draw_image_quad_persp(const fx_image_t *img, int x1, int y1, int x2, int y2,
                              float top_inset, float top_shift)
{
    float w = (float)(x2 - x1), h = (float)(y2 - y1);
    float in = top_inset * w * 0.5f;      /* 顶部左右各内缩 */
    float sh = top_shift * w;             /* 顶部整体水平偏移 */
    float xy8[8] = {
        (float)x1 + in + sh, (float)y1,
        (float)x2 - in + sh, (float)y1,
        (float)x2,           (float)y2,
        (float)x1,           (float)y2,
    };
    fx_draw_image_quad(img, xy8);
}
int fxtk_is_offing(void){ return s_offing; }
void fxtk_text_blit(void *tex,int x,int y,int w,int h)
{
    if(!s_drv||!s_drv->blit_tex)return;
    flush_line();   /* v2.3.1: 文字 blit 绕过行缓冲, 先刷滞留行防顺序颠倒 */
    int x1=x>s_clip_x1?x:s_clip_x1;
    int y1=y>s_clip_y1?y:s_clip_y1;
    int x2=(x+w-1)<s_clip_x2?(x+w-1):s_clip_x2;
    int y2=(y+h-1)<s_clip_y2?(y+h-1):s_clip_y2;
    if (x1>x2||y1>y2)return;
    s_drv->blit_tex(tex,x1-x,y1-y,x2-x1+1,y2-y1+1,x1+s_ox,y1+s_oy);
}

int fxtk_image_rot_gpu(const fx_image_t *img,int cx,int cy,int dw,int dh,double ang)
{
    if(s_offing||!s_drv||!s_drv->blit_img_rot||!img||!img->px)return 0;
    flush_line();   /* v2.3.1: 旋转 blit 同样绕过行缓冲 */
    if (s_drv->set_clip_rect) s_drv->set_clip_rect(s_clip_x1+s_ox,s_clip_y1+s_oy,s_clip_x2+s_ox,s_clip_y2+s_oy);   /* 画布裁剪 */
    s_drv->blit_img_rot(img->px,img->w,img->h,cx+s_ox,cy+s_oy,dw,dh,ang);
    if (s_drv->set_clip_rect) s_drv->set_clip_rect(0,0,32767,32767);
    return 1;
}

void fxtk_dbg_get_off(int*a,int*b){ *a=s_ox; *b=s_oy; }
