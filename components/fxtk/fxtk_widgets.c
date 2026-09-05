#include <math.h>
/**
 * fxtk_widgets.c — 控件绘制实现 (复选框居中+透明背景修复)
 */
#include "fxtk_internal.h"
#include "fxtk_desktop.h"
#include <string.h>
#include <stdio.h>

void fxtk_apply_fit(fx_widget_t *w);
static fx_color_t darken(fx_color_t c)
{
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return (fx_color_t)((r / 2) << 16 | (g / 2) << 8 | (b / 2));
}

static fx_color_t btn_mix(fx_color_t a, fx_color_t b, int t)
{   /* t: 0~256, b 的占比 */
    int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255;
    int br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
    int r=(ar*(256-t)+br*t+128)>>8, g=(ag*(256-t)+bg*t+128)>>8, bl=(ab*(256-t)+bb*t+128)>>8;
    return (fx_color_t)((r<<16)|(g<<8)|bl);
}
#if FXTK_WIDGET_BUTTON
void fxtk_draw_button(fx_widget_t *w)
{
    /* Adwaita 经典: 纯平圆角 + 锐利1px高光/阴影 (锐线不产生灰阶) */
    int cw = w->x2-w->x1+1, ch = w->y2-w->y1+1;
    int pr = (fx_pressed() == w);
    int r = 4; if (r > ch/2) r = ch/2; if (r > cw/2) r = cw/2;
    fx_set_color(w->bg);
    fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, r);
    if (!pr) {
        fx_set_color(btn_mix(w->bg, FX_WHITE, 90));          /* 顶高光 */
        fx_draw_hline(w->x1 + r, w->x2 - r, w->y1 + 1);
        fx_set_color(btn_mix(w->bg, FX_BLACK, 60));          /* 底阴影 */
        fx_draw_hline(w->x1 + r, w->x2 - r, w->y2 - 1);
    } else {
        fx_set_color(btn_mix(w->bg, FX_BLACK, 90));          /* 按下: 顶变阴影=内凹 */
        fx_draw_hline(w->x1 + r, w->x2 - r, w->y1 + 1);
    }
    fx_set_color(pr ? FX_RGB(60, 60, 60) : darken(w->bg));   /* 1px 同系深边 */
    fx_draw_hline(w->x1 + r, w->x2 - r, w->y1);
    fx_draw_hline(w->x1 + r, w->x2 - r, w->y2);
    fx_draw_vline(w->x1, w->y1 + r, w->y2 - r);
    fx_draw_vline(w->x2, w->y1 + r, w->y2 - r);
    if (w->title[0]) {
        int fs = w->lines > 0 ? w->lines : 0;
        int th = fs > 0 ? fs + 4 : 18;
        int tw = fxtk_text_width_size(fs, w->title);
        if (fs <= 0) fx_draw_text_c(w->x1 + (cw-tw)/2 + pr, w->y1 + (ch-18)/2 + pr,
                          w->title, w->fg, w->bg);
        else fxtk_draw_text_size(fs, w->x1 + (cw-tw)/2 + pr, w->y1 + (ch-th)/2 + pr,
                          w->title, w->fg, w->bg);
    }
}

#endif
#if FXTK_WIDGET_LABEL
void fxtk_draw_label(fx_widget_t *w)
{
    /* line(n)=字号(缺省18), row(0/1/2)=左/中/右, 垂直自动居中 */
    if (!w->title[0]) return;
    int fs = w->lines > 0 ? w->lines : 0;
    int cw = w->x2-w->x1+1, ch = w->y2-w->y1+1;
    int tw = fxtk_text_width_size(fs, w->title);
    int th = fs > 0 ? fs + 4 : 18;
    int x = w->x1;
    if (w->rows == 1) x = w->x1 + (cw - tw) / 2;
    else if (w->rows == 2) x = w->x2 - tw;
    if (w->bg != FX_BLACK) { fx_set_color(w->bg); fx_fill_rect(w->x1, w->y1, w->x2, w->y2); }
    fx_color_t tbg = (w->bg == FX_BLACK) ? fx_get_bg() : w->bg;
    if (fs <= 0) fx_draw_text_c(x, w->y1 + (ch - 18) / 2, w->title, w->fg, tbg);
    else fxtk_draw_text_size(fs, x, w->y1 + (ch - th) / 2 + 2, w->title, w->fg, tbg);
}

#endif
#if FXTK_WIDGET_GRID
void fxtk_draw_grid(fx_widget_t *w)
{
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    if (w->lines > 0 && w->rows > 0) {
        fx_set_color(fxtk_grid_lines_on() ? w->fg : w->bg);
        int cellw = (w->x2 - w->x1 + 1) / w->rows;
        int cellh = (w->y2 - w->y1 + 1) / w->lines;
        for (int r = 1; r < w->lines; r++) fx_draw_hline(w->x1, w->x2, w->y1 + r * cellh);
        for (int c = 1; c < w->rows; c++) fx_draw_vline(w->x1 + c * cellw, w->y1, w->y2);
    }
    if (w->border > 0) {
        fx_set_color(fxtk_grid_lines_on() ? w->fg : w->bg);
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

#endif
#if FXTK_WIDGET_CANVAS
void fxtk_draw_canvas(fx_widget_t *w)
{
    fxtk_apply_fit(w);   /* 铺底前强制收拢, 杜绝二次变大 */
    fx_set_color(w->bg);
    /* 离屏模式下用本地坐标(0,0)-(offw-1,offh-1); 正常模式用屏幕坐标 */
    if (w->offbuf && (w->flags & FX_F_BUF)) {
        fx_fill_rect(0, 0, w->offw - 1, w->offh - 1);
    } else {
        fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    }
    if (w->border > 0) {
        fx_set_color(darken(w->bg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

#endif
#if FXTK_WIDGET_SLIDER
void fxtk_draw_slider(fx_widget_t *w)
{
    int h = w->y2 - w->y1 + 1;
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    int track_y = w->y1 + h / 2 - 2;
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, track_y, w->x2, track_y + 3);
    int rw = w->x2 - w->x1 + 1;
    int filled = rw * w->value / 100;
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, track_y, w->x1 + filled - 1, track_y + 3);
    int kx = w->x1 + filled - 3;
    if (kx < w->x1) kx = w->x1;
    if (kx > w->x2 - 6) kx = w->x2 - 6;
    fx_set_color((w->flags & FX_F_PRESSED) ? darken(w->bg) : w->bg);
    fx_fill_rect(kx, w->y1, kx + 6, w->y2);
}

#endif
#if FXTK_WIDGET_PROGRESS
void fxtk_draw_progress(fx_widget_t *w)
{
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    int rw = w->x2 - w->x1 + 1;
    int filled = rw * w->value / 100;
    if (filled > 0) {
        fx_set_color(w->bg);
        fx_fill_rect(w->x1, w->y1, w->x1 + filled - 1, w->y2);
    }
    if (w->border > 0) {
        fx_set_color(darken(w->fg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

/* 【修复】复选框: 默认透明背景不铺色块; 方框+文字垂直居中 (缩放后不变形) */
#endif
#if FXTK_WIDGET_CHECKBOX
void fxtk_draw_checkbox(fx_widget_t *w)
{
    int h = w->y2 - w->y1 + 1;
    int box = h > 20 ? 20 : h;
    int by = w->y1 + (h - box) / 2;

    if (w->bg != FX_BLACK) {                 /* 只有显式指定 color() 才铺底 */
        fx_set_color(w->bg);
        fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    }
    fx_set_color(w->fg);
    fx_draw_rect(w->x1, by, w->x1 + box - 1, by + box - 1);
    if (w->value) {
        fx_draw_line(w->x1 + 3, by + box / 2, w->x1 + box / 2 - 1, by + box - 4);
        fx_draw_line(w->x1 + box / 2 - 1, by + box - 4, w->x1 + box - 4, by + 2);
    }
    if (w->title[0]) {
        fx_color_t text_bg = (w->bg != FX_BLACK) ? w->bg : fx_get_bg();
        fx_draw_text_c(w->x1 + box + 6, by, w->title, w->fg, text_bg);
    }
}

#endif
#if FXTK_WIDGET_PANEL
void fxtk_draw_panel(fx_widget_t *w)
{
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    if (w->border > 0) {
        fx_set_color(darken(w->bg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

#endif
#if FXTK_WIDGET_TAB
void fxtk_draw_tab(fx_widget_t *w)
{
    int side = w->tab_side;
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    int n = w->lines > 0 ? w->lines : 1;
    const char *p = w->title;
    for (int i = 0; i < n && p[0]; i++) {
        int tx1, ty1, tx2, ty2;
        if (side == FX_TAB_LEFT || side == FX_TAB_RIGHT) {   /* 垂直侧边栏 */
            int th = (w->y2 - w->y1 + 1) / n;
            ty1 = w->y1 + i * th; ty2 = (i == n - 1) ? w->y2 : ty1 + th - 1;
            if (side == FX_TAB_LEFT) { tx1 = w->x1; tx2 = w->x1 + FX_TAB_SIDE - 1; }
            else                    { tx1 = w->x2 - FX_TAB_SIDE + 1; tx2 = w->x2; }
        } else {                                            /* 水平标签条 */
            int tw = (w->x2 - w->x1 + 1) / n;
            tx1 = w->x1 + i * tw; tx2 = (i == n - 1) ? w->x2 : tx1 + tw - 1;
            if (side == FX_TAB_BOTTOM) { ty1 = w->y2 - FX_TAB_H + 1; ty2 = w->y2; }
            else                      { ty1 = w->y1; ty2 = w->y1 + FX_TAB_H - 1; }
        }
        int sel = (i == w->value);
        fx_color_t bg = sel ? btn_mix(w->bg, FX_WHITE, 24) : darken(w->bg);
        fx_set_color(bg);
        fx_fill_rect(tx1, ty1, tx2, ty2);
        /* 立体感: 选中=凸起(上+左高光), 未选中=下凹 */
        fx_set_color(sel ? btn_mix(bg, FX_WHITE, 90) : darken(bg));
        fx_draw_vline(tx1, ty1, ty2);   fx_draw_hline(tx1, tx2, ty1);
        fx_set_color(sel ? btn_mix(bg, FX_WHITE, 40) : darken(bg));
        fx_draw_hline(tx1, tx2, ty2);   fx_draw_vline(tx2, ty1, ty2);
        const char *comma = strchr(p, ',');
        char seg[96];
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 95) len = 95;
        memcpy(seg, p, (size_t)len);
        seg[len] = 0;
        int sw = fx_text_width(seg);
        fx_draw_text_c(tx1 + (tx2 - tx1 + 1 - sw) / 2, ty1 + (ty2 - ty1 - 16) / 2,
                       seg, sel ? FX_RGB(40, 40, 40) : FX_LGRAY, bg);
        p = comma ? comma + 1 : p + strlen(p);
    }
    /* 标签条与内容区之间: 深阴影 + 高光, 增强分层 (按方位) */
    fx_set_color(darken(w->bg));
    if (side == FX_TAB_LEFT)       fx_draw_vline(w->x1 + FX_TAB_SIDE, w->y1, w->y2);
    else if (side == FX_TAB_RIGHT) fx_draw_vline(w->x2 - FX_TAB_SIDE, w->y1, w->y2);
    else if (side == FX_TAB_BOTTOM)fx_draw_hline(w->x1, w->x2, w->y2 - FX_TAB_H);
    else                           fx_draw_hline(w->x1, w->x2, w->y1 + FX_TAB_H);
    fx_set_color(btn_mix(w->bg, FX_WHITE, 60));
    if (side == FX_TAB_LEFT)       fx_draw_vline(w->x1 + FX_TAB_SIDE + 1, w->y1, w->y2);
    else if (side == FX_TAB_RIGHT) fx_draw_vline(w->x2 - FX_TAB_SIDE - 1, w->y1, w->y2);
    else if (side == FX_TAB_BOTTOM)fx_draw_hline(w->x1, w->x2, w->y2 - FX_TAB_H + 1);
    else                           fx_draw_hline(w->x1, w->x2, w->y1 + FX_TAB_H + 1);
}

/* ---------- 图片控件 (交互: 按压缩暗 + 缩放) ---------- */
#endif
#if FXTK_WIDGET_IMAGE
void fxtk_draw_image(fx_widget_t *w)
{
    if (w->bg != FX_BLACK) {
        fx_set_color(w->bg);
        fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    }
    if (!w->img) {
        fx_set_color(FX_GRAY);
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
        fx_draw_line(w->x1, w->y1, w->x2, w->y2);
        fx_draw_line(w->x2, w->y1, w->x1, w->y2);
        return;
    }
    int dw0 = w->x2 - w->x1 + 1, dh0 = w->y2 - w->y1 + 1;
    int zoom = w->value > 0 ? w->value : 100;
    int dw = dw0 * zoom / 100, dh = dh0 * zoom / 100;
    int x = w->x1 + (dw0 - dw) / 2, y = w->y1 + (dh0 - dh) / 2;
    fx_draw_image_ex(w->img, x, y, dw, dh, (w->flags & FX_F_PRESSED));
}
/* ---------- 输入框 (桌面扩展) ---------- */
#endif
#if FXTK_WIDGET_TEXTEDIT
static int te_nx(const char *s, int off)
{
    int len = (int)strlen(s), i2 = off + 1;
    while (i2 < len && (((unsigned char)s[i2] & 0xC0) == 0x80)) i2++;
    return i2 > len ? len : i2;
}
void fxtk_draw_textedit(fx_widget_t *w)
{
    fxtk_font_set_size(w->lines > 0 ? w->lines : 18);   /* 固定字号: 文字/光标宽度用同一字体 */
    int focused = (fx_get_focus() == w);
    fx_color_t bg = (w->bg != FX_BLACK) ? w->bg : FX_WHITE;
    fx_color_t fg = (w->fg == FX_WHITE) ? FX_BLACK : w->fg;
    const char *txt = w->text_buf ? w->text_buf : w->title;
    int len = (int)strlen(txt);
    fx_set_color(bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    fx_set_color(focused ? FX_RGB(33, 150, 243) : FX_GRAY);
    fx_draw_rect(w->x1, w->y1, w->x2, w->y2);

    int tx = w->x1 + 6;
    int aw = (w->x2 - w->x1 + 1) - 12;
    int lh = 22;

    /* 自动换行: 计算每行起点 */
    static int st[512]; static int se[512];
    int nl = 1; st[0] = 0; se[0] = len;
    int acc = 0, i2 = 0;
    while (i2 < len && nl < 511) {
        if (txt[i2] == '\n') { se[nl-1] = i2; st[nl] = i2+1; se[nl] = len; nl++; acc = 0; i2++; continue; }
        int j = te_nx(txt, i2);
        int cw = fx_text_width_n(txt + i2, j - i2);
        if (acc + cw > aw && j > st[nl - 1]) { se[nl-1] = i2; st[nl] = i2; se[nl] = len; nl++; acc = 0; continue; }
        acc += cw; i2 = j;
    }
    se[nl-1] = len;
    int total_h = nl * lh + 8;
    w->content_h = (int16_t)(total_h > 32000 ? 32000 : total_h);
    int vis_h = w->y2 - w->y1 - 10;
    if (w->scroll_y < 0) w->scroll_y = 0;
    if (w->scroll_y > total_h - vis_h && total_h > vis_h) w->scroll_y = (int16_t)(total_h - vis_h);

    int a = w->caret, b = w->anchor;
    if (a > b) { int t = a; a = b; b = t; }

    /* 逐行绘制 (只画可见行) */
    int ty0 = w->y1 + 5 - w->scroll_y;
    for (int L = 0; L < nl; L++) {
        int y = ty0 + L * lh;
        if (y + lh < w->y1 + 2 || y > w->y2 - 2) continue;
        int s0 = st[L];
        int s1 = se[L];
        fx_draw_text_c_n(tx, y, txt + s0, s1 - s0, fg, bg);
        if (b > a) {
            int hs = s0 > a ? s0 : a, he = s1 < b ? s1 : b;
            if (he > hs) {
                int wxs = fx_text_width_n(txt + s0, hs - s0);
                int wxe = fx_text_width_n(txt + s0, he - s0);
                fx_set_color(FX_RGB(33, 150, 243));
                fx_fill_rect(tx + wxs, y - 1, tx + wxe, y + lh - 2);
                fx_draw_text_c_n(tx + wxs, y, txt + hs, he - hs, FX_WHITE, FX_RGB(33, 150, 243));
            }
        }
    }
    /* 光标: 定位到所在行 */
    if (focused && (fx_focus_blink() || b > a)) {
        int Lc = 0;
        for (int L = 0; L < nl; L++) if (w->caret >= st[L]) Lc = L;
        int cxn = fx_text_width_n(txt + st[Lc], w->caret - st[Lc]);
        int cx = tx + cxn;
        int cy = ty0 + Lc * lh;
        fx_set_color(fg);
        fx_fill_rect(cx, cy - 1, cx + 1, cy + 20);   /* 2px 光标: 与文字完整高度对齐(含descender), 右缘对齐字符边界 */
    }
/* TE-SCROLLBAR */
if (total_h > vis_h) {
int rw2 = w->x2 - 2;
int th = vis_h * vis_h / total_h; if (th < 20) th = 20;
int ty = w->y1 + 2 + (int)((long)w->scroll_y * (w->y2 - w->y1 - 4 - th) / (total_h - vis_h));
fx_set_color(FX_RGB(200, 200, 200));
fx_fill_rect(rw2 - 3, w->y1 + 2, rw2, w->y2 - 2);
fx_set_color(FX_RGB(120, 120, 120));
fx_fill_rect(rw2 - 3, ty, rw2, ty + th);
}
    if (w->text_max > 0) {
        int n = 0;
        for (int k = 0; k < len; n++) {
            int j = te_nx(txt, k); k = j;
        }
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "%d/%d", n, w->text_max);
        int cw2 = fx_text_width(cnt);
        fx_draw_text_c(w->x2 - cw2 - 6, w->y1 + 4, cnt, FX_GRAY, bg);
    }
}

#endif