/**
 * fxtk_wm.c — v3 多窗口管理器 (画布内直接绘制, 无离屏/无图像缩放)
 *
 * 关键点: 每个窗口挂一块"不可见画布"当坐标基准。画内容时调用
 *   fx_canvas_begin(win->cv)  →  s_ox/s_oy = 画布位置, clip = 画布大小
 * 于是场景的立即模式绘制(含文字)会原样落到窗口内容区, 不必离屏渲染、
 * 不必缩放图片, 分辨率怎么变都清晰。
 */
#include "fxtk_wm.h"
#include "fxtk_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WM_MAXW 32

typedef struct {
    int used;
    char title[80];
    int style, z, focus, visible;
    float x, y, w, h;
    fx_widget_t *cv;
} W;

static W s_w[WM_MAXW];
static int s_n = 0;
static float s_ui = 1.f;

fx_color_t fx_wm_blendc(fx_color_t a, fx_color_t b, int k)
{
    if (k < 0) k = 0; if (k > 255) k = 255;
    int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255, br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
    return FX_RGB(ar+(br-ar)*k/255, ag+(bg-ag)*k/255, ab+(bb-ab)*k/255);
}

int fx_wm_title_h(int style)
{
    switch (style) {
    case FX_WM_WIN11: return (int)(34 * s_ui);
    case FX_WM_TERM:  return (int)(26 * s_ui);
    case FX_WM_PLAIN: return 0;
    default:          return (int)(36 * s_ui);
    }
}

int fx_wm_add(const char *title, int style, int cw, int ch)
{
    (void)cw; (void)ch;
    if (s_n >= WM_MAXW) return -1;
    W *w = &s_w[s_n];
    memset(w, 0, sizeof(*w));
    w->used = 1; w->style = style; w->z = s_n; w->visible = 1;
    snprintf(w->title, sizeof w->title, "%s", title ? title : "");
    char nm[24];
    snprintf(nm, sizeof nm, "wmc%d", s_n);
    w->cv = fx_canvas_new(pixel("0,0","1,1"), anim(0), name(nm), color(FX_RGB(0,0,0)), call(0));
    if (!w->cv) return -1;
    w->cv->flags &= (uint16_t)~FX_F_VISIBLE;   /* 不作为控件绘制, 只当坐标基准 */
    return s_n++;
}

void fx_wm_set(int idx, float x, float y, float w, float h)
{
    if (idx < 0 || idx >= s_n) return;
    s_w[idx].x = x; s_w[idx].y = y; s_w[idx].w = w; s_w[idx].h = h;
}
void fx_wm_set_style(int idx, int st) { if (idx >= 0 && idx < s_n) s_w[idx].style = st; }
void fx_wm_set_title(int idx, const char *t) { if (idx >= 0 && idx < s_n) snprintf(s_w[idx].title, sizeof s_w[idx].title, "%s", t ? t : ""); }
void fx_wm_set_z(int idx, int z) { if (idx >= 0 && idx < s_n) s_w[idx].z = z; }
void fx_wm_set_focus(int idx, int f) { if (idx >= 0 && idx < s_n) s_w[idx].focus = f; }
void fx_wm_set_visible(int idx, int v) { if (idx >= 0 && idx < s_n) s_w[idx].visible = v; }
void fx_wm_set_rot(int idx, float deg) { (void)deg; }
void fx_wm_set_ui(float k) { s_ui = k > 0.2f ? k : 0.2f; }
int fx_wm_count(void) { return s_n; }
void fx_wm_get(int idx, float *x, float *y, float *w, float *h, int *style, int *focus)
{
    if (idx < 0 || idx >= s_n) return;
    if (x) *x = s_w[idx].x; if (y) *y = s_w[idx].y;
    if (w) *w = s_w[idx].w; if (h) *h = s_w[idx].h;
    if (style) *style = s_w[idx].style; if (focus) *focus = s_w[idx].focus;
}

void fx_wm_prepare(fx_wm_content_fn content, void *ud) { (void)content; (void)ud; }

static fx_color_t body_col(int st)
{
    switch (st) {
    case FX_WM_WIN11: return FX_RGB(250, 250, 251);
    case FX_WM_TERM:  return FX_RGB(7, 9, 13);
    case FX_WM_PLAIN: return FX_RGB(8, 10, 15);
    default:          return FX_RGB(16, 18, 24);
    }
}
static fx_color_t bar_col(int st, int focus)
{
    switch (st) {
    case FX_WM_WIN11: return focus ? FX_RGB(244, 244, 245) : FX_RGB(236, 236, 238);
    case FX_WM_TERM:  return FX_RGB(13, 16, 21);
    case FX_WM_PLAIN: return FX_RGB(8, 10, 15);
    default:          return focus ? FX_RGB(28, 32, 42) : FX_RGB(22, 25, 33);
    }
}

static void draw_title(W *w, int x, int y, int ww, int ttl, int r)
{
    int st = w->style;
    fx_color_t bc = bar_col(st, w->focus);
    fx_set_color(bc);
    fx_fill_rect_round(x, y, x + ww - 1, y + ttl + r - 1, r);
    fx_fill_rect(x, y + ttl - 1, x + ww - 1, y + ttl + r - 1);
    fx_fill_rect(x + 1, y + 1, x + ww - 2, y + ttl - 1);
    fx_set_color(fx_wm_blendc(bc, FX_RGB(255,255,255), 30));
    fx_draw_hline(x + r, x + ww - 1 - r, y + 1);
    fx_set_color(fx_wm_blendc(bc, FX_RGB(0,0,0), 80));
    fx_draw_hline(x + 1, x + ww - 2, y + ttl - 1);

    int fs = (int)(13 * s_ui);
    if (st == FX_WM_WIN11) {
        fxtk_draw_text_size(fs, x + (int)(14*s_ui), y + (ttl - (int)(16*s_ui))/2, w->title, FX_RGB(32,33,38), bc);
        int bw = (int)(44*s_ui), bx = x + ww - 3*bw;
        fx_set_color(FX_RGB(80,80,86));
        fx_draw_hline(bx + bw/2 - 6, bx + bw/2 + 6, y + ttl/2);
        fx_draw_rect(bx + bw + bw/2 - 5, y + ttl/2 - 5, bx + bw + bw/2 + 5, y + ttl/2 + 5);
        fx_set_color(FX_RGB(200,45,35));
        fx_draw_hline(bx + 2*bw + bw/2 - 6, bx + 2*bw + bw/2 + 6, y + ttl/2 - 4);
        fx_draw_hline(bx + 2*bw + bw/2 - 6, bx + 2*bw + bw/2 + 6, y + ttl/2 + 4);
    } else if (st == FX_WM_TERM) {
        fxtk_draw_text_size((int)(12*s_ui), x + (int)(10*s_ui), y + (ttl - (int)(15*s_ui))/2, w->title, FX_RGB(110,200,140), bc);
        int d=(int)(9*s_ui), gap=(int)(6*s_ui), by=y+ttl/2-d/2;
        fx_set_color(FX_RGB(120,70,70));  fx_fill_rect_round(x+ww-3*(d+gap), by, x+ww-3*(d+gap)+d, by+d, d/2);
        fx_set_color(FX_RGB(140,130,60)); fx_fill_rect_round(x+ww-2*(d+gap), by, x+ww-2*(d+gap)+d, by+d, d/2);
        fx_set_color(FX_RGB(70,140,80));  fx_fill_rect_round(x+ww-1*(d+gap), by, x+ww-1*(d+gap)+d, by+d, d/2);
    } else {
        int tw = (int)strlen(w->title) * (int)(7*s_ui);
        int tx = x + (ww - tw)/2; if (tx < x + (int)(70*s_ui)) tx = x + (int)(70*s_ui);
        fxtk_draw_text_size(fs, tx, y + (ttl - (int)(16*s_ui))/2, w->title, FX_RGB(225,231,242), bc);
        int d=(int)(9*s_ui), gap=(int)(7*s_ui), by2=y+ttl/2-d/2;
        fx_set_color(FX_RGB(235,95,92)); fx_fill_rect_round(x+(int)(13*s_ui), by2, x+(int)(13*s_ui)+d, by2+d, d/2);
        fx_set_color(FX_RGB(240,188,90)); fx_fill_rect_round(x+(int)(13*s_ui)+d+gap, by2, x+(int)(13*s_ui)+2*d+gap, by2+d, d/2);
        fx_set_color(FX_RGB(100,200,120)); fx_fill_rect_round(x+(int)(13*s_ui)+2*(d+gap), by2, x+(int)(13*s_ui)+3*d+2*gap, by2+d, d/2);
        if (w->focus) { fx_set_color(FX_RGB(64,208,224)); fx_draw_hline(x+r, x+ww-1-r, y+ttl); }
    }
}

void fx_wm_draw(fx_wm_content_fn content, void *ud)
{
    int order[WM_MAXW], n = 0;
    for (int i = 0; i < s_n; i++) if (s_w[i].used && s_w[i].visible) order[n++] = i;
    for (int i = 1; i < n; i++) {
        int k = order[i], j = i - 1;
        while (j >= 0 && s_w[order[j]].z > s_w[k].z) { order[j+1] = order[j]; j--; }
        order[j+1] = k;
    }
    for (int oi = 0; oi < n; oi++) {
        W *w = &s_w[order[oi]];
        int x=(int)w->x, y=(int)w->y, ww=(int)w->w, hh=(int)w->h;
        if (ww < 24 || hh < 24) continue;
        int ttl = fx_wm_title_h(w->style);
        if (ttl > hh - 8) ttl = hh - 8;
        int r = (int)((w->style == FX_WM_WIN11 ? 10 : w->style == FX_WM_TERM ? 7 : 9) * s_ui);
        if (r * 2 > ww) r = ww / 2;
        if (r * 2 > hh) r = hh / 2;
        fx_color_t bd = (w->style == FX_WM_WIN11) ? FX_RGB(206,207,210)
                      : (w->focus ? FX_RGB(56,96,116) : FX_RGB(38,43,54));
        fx_set_color(bd);
        fx_fill_rect_round(x, y, x + ww - 1, y + hh - 1, r);
        fx_set_color(body_col(w->style));
        fx_fill_rect_round(x + 1, y + 1, x + ww - 2, y + hh - 2, r > 1 ? r - 1 : 0);
        if (ttl > 0) draw_title(w, x + 1, y + 1, ww - 2, ttl, r > 1 ? r - 1 : 0);
        int cx1=x+2, cy1=y+ttl+1, cx2=x+ww-3, cy2=y+hh-3;
        if (cx2 - cx1 > 8 && cy2 - cy1 > 8 && content) {
            fx_widget_set_rect(w->cv, cx1, cy1, cx2, cy2);
            fx_canvas_begin(w->cv);
            content(order[oi], cx2 - cx1 + 1, cy2 - cy1 + 1, ud);
            fx_canvas_end();
        }
    }
}
