#include <math.h>
/**
 * fxtk_widgets.c — 控件绘制实现 (复选框居中+透明背景修复)
 */
#include "fxtk_internal.h"
#include "fxtk_backends.h"   /* v2.4.3: fx_log 告警(动画槽位满时提示加大宏) */
#include "fxtk_tokens.h"
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
    /* P5 审美迭代 1/9: 圆角走设计令牌(4 → FX_TOK_RADIUS_M=6, 边缘更柔和);
     * 按下态不再只压暗顶边, 而是整块略压暗 —— 触摸屏上"按没按到"更容易一眼看出。 */
    int r = FX_TOK_RADIUS_BTN;
    if (r > ch/2) r = ch/2; if (r > cw/2) r = cw/2;
    fx_set_color(pr ? btn_mix(w->bg, FX_BLACK, 12) : w->bg);
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
    fx_set_color(pr ? FX_TOK_EDGE_DOWN : btn_mix(w->bg, FX_BLACK, FX_TOK_BTN_EDGE_MIX));   /* 1px 同系深边(加深以提高分离度) */
    fx_draw_hline(w->x1 + r, w->x2 - r, w->y1);
    fx_draw_hline(w->x1 + r, w->x2 - r, w->y2);
    fx_draw_vline(w->x1, w->y1 + r, w->y2 - r);
    fx_draw_vline(w->x2, w->y1 + r, w->y2 - r);
    if (w->title[0]) {
        int fs = w->lines > 0 ? w->lines : 0;
        int th = fs > 0 ? fs + 4 : 18;
        int tw = fxtk_text_width_size(fs, w->title);
        /* v2.4.2: 按下时按钮整体压暗了, 但文字块的底色还是原来的 w->bg → 文字后面留一块"旧色"的方块
         * (用户反馈"按下按钮后字体背景颜色未及时改变")。让文字底色跟随按下态一起变。 */
        fx_color_t lbl_bg = pr ? btn_mix(w->bg, FX_BLACK, 12) : w->bg;
        if (fs <= 0) fx_draw_text_c(w->x1 + (cw-tw)/2 + pr, w->y1 + (ch-18)/2 + pr,
                          w->title, w->fg, lbl_bg);
        else fxtk_draw_text_size(fs, w->x1 + (cw-tw)/2 + pr, w->y1 + (ch-th)/2 + pr,
                          w->title, w->fg, lbl_bg);
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
    /* P5 审美迭代 9/9(标签): 带底色的"色块标签"(如"颜色显示区"那种色条)原为硬直角, 与其它控件不一致。
     * 圆角要【自保护】: 文字是用 fx_draw_text_c 铺一块底色矩形画上去的, 若文字块够到四角就会把圆角重新切方 ——
     * 所以只在"文字块四周留白 ≥ 圆角半径"时才圆, 否则保持方形(宁可不圆, 也不出现半圆角)。 */
    int rad = 0;
    if (w->bg != FX_BLACK) {
        int r = FX_TOK_RADIUS_M;      /* 色块标签用中号圆角: 小号在 50px 高的色条上几乎看不出来 */
        if (cw - tw >= r * 2 && ch - th >= r * 2) rad = r;
        if (rad > ch / 2) rad = ch / 2;
        if (rad > cw / 2) rad = cw / 2;
        fx_set_color(w->bg);
        if (rad > 0) fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, rad);
        else         fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    }
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
/* v2.4 视觉修正 (视觉评审指出旧实现像"坏掉的进度条": 整块 fg 铺底 + 4px 细线 + 全高竖条):
 * 现在按现代滑条画 —— 不铺底、居中 6px 圆角轨道(未填充部分用浅灰)、已填充段用主题色、
 * 右侧 14px 宽圆角滑块带 1px 深色描边与按下位移。 */
void fxtk_draw_slider(fx_widget_t *w)
{
    int h = w->y2 - w->y1 + 1;
    int cy = w->y1 + h / 2;
    int th = h / 4;
    if (th < FX_TOK_TRACK_H_MIN) th = FX_TOK_TRACK_H_MIN; if (th > FX_TOK_TRACK_H_MAX) th = FX_TOK_TRACK_H_MAX;
    int ty0 = cy - th / 2, ty1 = ty0 + th - 1;
    int rw = w->x2 - w->x1 + 1;
    int kw = h / 2;
    if (kw < FX_TOK_KNOB_W_MIN) kw = FX_TOK_KNOB_W_MIN; if (kw > FX_TOK_KNOB_W_MAX) kw = FX_TOK_KNOB_W_MAX;
    int kx = w->x1 + rw * w->value / 100;
    if (kx < w->x1 + kw / 2) kx = w->x1 + kw / 2;
    if (kx > w->x2 - kw / 2) kx = w->x2 - kw / 2;

    fx_set_color(FX_TOK_TRACK);                                  /* 轨道底色 */
    fx_fill_rect_round(w->x1, ty0, w->x2, ty1, th / 2);
    fx_set_color(w->bg);                                                  /* 已填充段 = 主题色 */
    if (kx > w->x1 + 1) fx_fill_rect_round(w->x1, ty0, kx - 1, ty1, th / 2);
    fx_set_color(FX_TOK_TRACK_EDGE);                                  /* 轨道描边 */
    fx_draw_hline(w->x1 + th / 2, w->x2 - th / 2, ty1);

    int pr = (w->flags & FX_F_PRESSED) ? 1 : 0;
    int kh = h - 4;
    if (kh < kw) kh = kw;                                 /* 滑块略高, 更易点 */
    int ky0 = cy - kh / 2 + pr, ky1 = ky0 + kh - 1;
    /* P5 审美迭代 3/9(滑杆): 滑块原本是"圆角填充 + 直角描边" —— 圆角被描边的直角切掉,
     * 看起来像方形贴了个圆角, 与按钮/输入框的圆角语言也不统一。描边改用同半径的圆角矩形,
     * 调用次数不变(1 填充 + 1 描边), 零额外开销。半径比例走令牌 FX_TOK_RADIUS_KNOB_DIV。 */
    int kr = kw / FX_TOK_RADIUS_KNOB_DIV;
    if (kr < 2) kr = 2;
    fx_set_color(FX_TOK_KNOB);
    fx_fill_rect_round(kx - kw / 2, ky0, kx + kw / 2, ky1, kr);
    fx_set_color(pr ? FX_TOK_KNOB_EDGE_DOWN : FX_TOK_KNOB_EDGE);
    fx_draw_rect_round(kx - kw / 2, ky0, kx + kw / 2, ky1, kr);
}

#endif
#if FXTK_WIDGET_PROGRESS
/* v2.4.3 动画: 进度条数值缓动(值突变时平滑增长)。默认全局关 → 行为与旧版完全一致。
 * 与标签淡入同样按【时间】推进: 同一帧内控件可能被绘制多次, 按帧累加会被重复推进(实测会卡在中途)。 */
#ifndef FX_ANIM_SLOTS
#define FX_ANIM_SLOTS 8       /* 同时可动画的控件数; 超出会明确告警(不再静默不动画) */
#endif
static fx_widget_t *s_pa_w[FX_ANIM_SLOTS];
static float    s_pa_from[FX_ANIM_SLOTS], s_pa_disp[FX_ANIM_SLOTS];
static int      s_pa_to[FX_ANIM_SLOTS];
static uint32_t s_pa_t0[FX_ANIM_SLOTS];
static int      s_pa_init[FX_ANIM_SLOTS];
#define FX_PROG_EASE_MS 180u
static int progress_anim_value(fx_widget_t *w)
{
    int k = -1;
    for (int i = 0; i < FX_ANIM_SLOTS; i++) if (s_pa_w[i] == w) { k = i; break; }
    if (k < 0) {
        for (int i = 0; i < FX_ANIM_SLOTS; i++) if (!s_pa_w[i]) { s_pa_w[i] = w; s_pa_init[i] = 0; k = i; break; }
        static int warned = 0;
        if (k < 0) {
            if (!warned) { fx_log(FX_LOG_WARN, "动画槽位已满(%d): 加大 FX_ANIM_SLOTS", FX_ANIM_SLOTS); warned = 1; }
            return w->value;
        }
    }
    if (!s_pa_init[k]) { s_pa_init[k] = 1; s_pa_disp[k] = (float)w->value; s_pa_to[k] = w->value; s_pa_t0[k] = (uint32_t)fx_time_ms(); }
    if (!fx_widget_anim_ok(w)) { w->flags &= (uint16_t)~FX_F_ANIM; s_pa_disp[k] = (float)w->value; s_pa_to[k] = w->value; return w->value; }
    if (w->value != s_pa_to[k]) {          /* 值变了: 从当前位置重新出发 */
        s_pa_from[k] = s_pa_disp[k]; s_pa_to[k] = w->value; s_pa_t0[k] = (uint32_t)fx_time_ms();
    }
    uint32_t el = (uint32_t)fx_time_ms() - s_pa_t0[k];
    if (el >= FX_PROG_EASE_MS) { w->flags &= (uint16_t)~FX_F_ANIM; s_pa_disp[k] = (float)s_pa_to[k]; return s_pa_to[k]; }
    w->flags |= FX_F_ANIM;
    float t = (float)el / (float)FX_PROG_EASE_MS;
    s_pa_disp[k] = s_pa_from[k] + ((float)s_pa_to[k] - s_pa_from[k]) * t;
    return (int)(s_pa_disp[k] + 0.5f);
}

void fxtk_draw_progress(fx_widget_t *w)
{
    /* P5 审美迭代 6/9(进度条): 轨道/填充/描边原为硬直角, 与其它控件不一致。改为圆角,
     * 填充的圆角按高度收敛(短填充不会因圆角过大而变形)。调用次数不变。 */
    int rw = w->x2 - w->x1 + 1;
    int rh = w->y2 - w->y1 + 1;
    int pr = rh / 2;
    if (pr > FX_TOK_RADIUS_M) pr = FX_TOK_RADIUS_M; if (pr < 2) pr = 2;
    fx_set_color(w->fg);
    fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, pr);
    int filled = rw * progress_anim_value(w) / 100;   /* v2.4.3: 动画开启时用缓动值 */
    if (filled > 0) {
        int fr = pr;
        if (fr > filled / 2) fr = filled / 2; if (fr < 1) fr = 1;
        fx_set_color(w->bg);
        fx_fill_rect_round(w->x1, w->y1, w->x1 + filled - 1, w->y2, fr);
    }
    if (w->border > 0) {
        fx_set_color(darken(w->fg));
        fx_draw_rect_round(w->x1, w->y1, w->x2, w->y2, pr);
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
    /* P5 审美迭代 7/9(复选框): 方框由硬直角改圆角(令牌 S), 与输入框/列表同一套语言;
     * 勾选标记仍是两条线(形状本身没问题), 调用次数不变。 */
    int cbr = FX_TOK_RADIUS_S;
    if (cbr > box / 3) cbr = box / 3; if (cbr < 1) cbr = 1;
    fx_set_color(w->fg);
    fx_draw_rect_round(w->x1, by, w->x1 + box - 1, by + box - 1, cbr);
    if (w->value) {
        fx_draw_line(w->x1 + 3, by + box / 2, w->x1 + box / 2 - 1, by + box - 4);
        fx_draw_line(w->x1 + box / 2 - 1, by + box - 4, w->x1 + box - 4, by + 2);
    }
    if (w->title[0]) {
        fx_color_t text_bg = (w->bg != FX_BLACK) ? w->bg : fx_get_bg();
        fx_draw_text_c(w->x1 + box + FX_TOK_TEXT_PAD_X, by, w->title, w->fg, text_bg);
    }
}

#endif
#if FXTK_WIDGET_PANEL
void fxtk_draw_panel(fx_widget_t *w)
{
    /* P5 审美迭代 8/9(面板/卡片): 面板一直是硬直角。圆角规则取"卡片 vs 底板"的实用判据 ——
     * 铺满整屏的面板(页面底板/根面板)保持方形, 否则圆角: 底板上露圆角会在四角透出窗口底色,
     * 那是视觉 bug 而不是美化; 卡片(通常是带描边的小面板)则应该圆。
     * 调用次数不变(填充/描边各 1 次, 只是换圆角版本)。 */
    int pw = w->x2 - w->x1 + 1, ph = w->y2 - w->y1 + 1;
    int full = (pw >= (int)fx_width() - 2 && ph >= (int)fx_height() - 2);
    int r = full ? 0 : FX_TOK_RADIUS_L;
    if (r > ph / 2) r = ph / 2;
    if (r > pw / 2) r = pw / 2;
    fx_set_color(w->bg);
    if (r > 0) fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, r);
    else       fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    if (w->border > 0) {
        fx_set_color(btn_mix(w->bg, FX_BLACK, 18));
        if (r > 0) fx_draw_rect_round(w->x1, w->y1, w->x2, w->y2, r);
        else       fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

#endif
#if FXTK_WIDGET_TAB
/* v2.4.3 动画: 标签选中药丸的"淡入"(只改颜色混合, 不动几何 → 不会出现文字/位置错位)。
 * 默认全局关; 演示里 fx_animation(1) 打开; 单个控件可用 anim(0) 关掉。 */
/* v2.4.3 动画: 标签选中药丸的"淡入"(只改颜色混合, 不动几何 → 不会出现文字/位置错位)。
 * 默认全局关; 演示里 fx_animation(1) 打开; 单个控件可用 anim(0) 关掉。
 * 进度按【时间】算而不是按帧累加 —— 同一帧内控件可能被绘制多次, 按帧累加会被重复推进/重复重置,
 * 表现为"淡入到一半就卡住"(实测停在 234 而非 250)。时间基准天然幂等。 */
static fx_widget_t *s_ta_w[FX_ANIM_SLOTS]; static uint32_t s_ta_t0[FX_ANIM_SLOTS]; static int s_ta_sel[FX_ANIM_SLOTS];
#define FX_TAB_FADE_MS 140u
static float tab_anim_mix(fx_widget_t *w)
{
    int k = -1;
    for (int i = 0; i < FX_ANIM_SLOTS; i++) if (s_ta_w[i] == w) { k = i; break; }
    if (k < 0) {
        for (int i = 0; i < FX_ANIM_SLOTS; i++) if (!s_ta_w[i]) { s_ta_w[i] = w; s_ta_t0[i] = (uint32_t)fx_time_ms(); s_ta_sel[i] = -9; k = i; break; }
        static int warned = 0;
        if (k < 0) { if (!warned) { fx_log(FX_LOG_WARN, "动画槽位已满(%d): 加大 FX_ANIM_SLOTS", FX_ANIM_SLOTS); warned = 1; } return 1.0f; }
    }
    if (s_ta_sel[k] == -9) s_ta_sel[k] = w->value;
    if (s_ta_sel[k] != w->value) { s_ta_sel[k] = w->value; s_ta_t0[k] = (uint32_t)fx_time_ms(); }
    if (!fx_widget_anim_ok(w)) { w->flags &= (uint16_t)~FX_F_ANIM; return 1.0f; }
    uint32_t el = (uint32_t)fx_time_ms() - s_ta_t0[k];
    if (el >= FX_TAB_FADE_MS) { w->flags &= (uint16_t)~FX_F_ANIM; return 1.0f; }   /* 结束: 交还静态绘制 */
    w->flags |= FX_F_ANIM;                                                        /* 进行中: 请求继续重绘 */
    return (float)el / (float)FX_TAB_FADE_MS;
}

void fxtk_draw_tab(fx_widget_t *w)
{
    float a_mix = 1.0f;
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
        /* P5 审美迭代 4/9(标签页): 原先是"每格铺色 + 4 条立体线"的方格倒角, 与按钮/输入框/滑杆的
         * 圆角语言不统一, 而且一格要 5 次绘制。改成: 选中 = 圆角胶囊高亮(内缩 2px 留出间隔),
         * 未选中不铺色、不画线(直接露出标签条底色)。视觉更干净, 且每格少 4 次绘制调用。 */
        int sel = (i == w->value);
        if (i == 0) a_mix = tab_anim_mix(w);      /* 每帧只算一次药丸淡入进度 */
        fx_color_t bg = w->bg;                  /* 文字底: 未选中与标签条同色 → 文字块自然融进去 */
        if (sel) {
            bg = btn_mix(w->bg, FX_WHITE, (int)(FX_TOK_TAB_PILL_MIX * a_mix));
            int padx = 2, pady = 2;
            int rr = (ty2 - ty1 + 1 - pady * 2) / 2;
            if (rr > FX_TOK_RADIUS_M) rr = FX_TOK_RADIUS_M;
            if (rr < 2) rr = 2;
            fx_set_color(bg);
            fx_fill_rect_round(tx1 + padx, ty1 + pady, tx2 - padx, ty2 - pady, rr);
        }
        const char *comma = strchr(p, ',');
        char seg[96];
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 95) len = 95;
        while (len > 0 && ((unsigned char)p[len] & 0xC0) == 0x80) len--;   /* v2.3.1: UTF-8 边界回退, 不切半字符 */
        memcpy(seg, p, (size_t)len);
        seg[len] = 0;
        int sw = fx_text_width(seg);
        /* v2.4.2: 低分辨率下(窗口缩到最小 480x272 时)标签会挤到越界/重叠(用户反馈"低分率下字体出界")。
         * 挤不下就自动降一档字号; 再挤不下就省略号收尾, 保证永不越出格子。 */
        int cellw = tx2 - tx1 + 1;
        int use_small = 0;
        if (sw > cellw - 4) { use_small = 1; sw = fxtk_text_width_size(12, seg); }
        if (sw > cellw - 4) {
            int n = (int)strlen(seg);
            while (n > 1) {
                n--;
                while (n > 0 && ((unsigned char)seg[n] & 0xC0) == 0x80) n--;
                if (fxtk_text_width_size(12, seg) <= cellw - 4) break;
                seg[n] = 0;
            }
            sw = fxtk_text_width_size(12, seg);
        }
        /* 文字配色: 未选中的格子现在直接露标签条底色(浅色), 原来的浅灰字会"糊"掉 ——
         * 实测肉眼几乎读不出来, 所以未选中改深灰(TEXT_DIM), 选中用正文色深灰压在浅色胶囊上。 */
        if (use_small)
            fxtk_draw_text_size(12, tx1 + (cellw - sw) / 2, ty1 + (ty2 - ty1 - 12) / 2,
                                seg, sel ? FX_TOK_TEXT : FX_TOK_TAB_TEXT, bg);
        else
            fx_draw_text_c(tx1 + (cellw - sw) / 2, ty1 + (ty2 - ty1 - 16) / 2,
                           seg, sel ? FX_TOK_TEXT : FX_TOK_TAB_TEXT, bg);
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
    /* P5 审美迭代 2/9(输入框): 原先输入框是硬直角, 而按钮/滑杆都是圆角 —— 同屏看就是"两套设计"。
     * 改成圆角填充 + 圆角描边: 与按钮同为 1 次填充 + 1 次描边, **零额外绘制调用**;
     * 圆角取 FX_TOK_RADIUS_S(输入框比按钮更"方"一点, 保持输入区的稳重感)。 */
    int er = FX_TOK_RADIUS_S;
    { int ech = w->y2 - w->y1 + 1, ecw = w->x2 - w->x1 + 1;
      if (er > ech / 2) er = ech / 2;
      if (er > ecw / 2) er = ecw / 2; }
    fx_set_color(bg);
    fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, er);
    fx_set_color(focused ? FX_TOK_PRIMARY : FX_GRAY);
    if (er > 0) fx_draw_rect_round(w->x1, w->y1, w->x2, w->y2, er);
    else        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);

    int tx = w->x1 + FX_TOK_TEXT_PAD_X;
    int aw = (w->x2 - w->x1 + 1) - FX_TOK_TEXT_PAD_X * 2;
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
                fx_set_color(FX_TOK_PRIMARY);
                fx_fill_rect(tx + wxs, y - 1, tx + wxe, y + lh - 2);
                fx_draw_text_c_n(tx + wxs, y, txt + hs, he - hs, FX_TOK_ON_PRIMARY, FX_TOK_PRIMARY);
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
        /* v2.4.1: 让输入法候选窗贴到光标下方。
         * 注意 cx/cy 已经是【屏幕绝对坐标】—— tx = w->x1 + PAD、ty0 = w->y1 + 5 - scroll
         * 都已含文本框原点; 早先这里又加了 w->x1/w->y1, 原点多算一遍 → 候选窗整体偏下一个框高。 */
        fx_set_ime_pos(cx, cy + 20);
    }
/* TE-SCROLLBAR */
if (total_h > vis_h) {
int rw2 = w->x2 - 2;
int th = vis_h * vis_h / total_h;
if (th < 20) th = 20;
int ty = w->y1 + 2 + (int)((long)w->scroll_y * (w->y2 - w->y1 - 4 - th) / (total_h - vis_h));
fx_set_color(FX_TOK_BORDER);
fx_fill_rect(rw2 - 3, w->y1 + 2, rw2, w->y2 - 2);
fx_set_color(FX_TOK_TEXT_DIM);
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