/**
 * fxtk.c — 规范终版: 核心 + 桌面扩展 (输入框/滚动/滚轮/图片/压测)
 * 直接整文件覆盖, 不要再打补丁!
 */
#include "fxtk_internal.h"
#include "fxtk_tokens.h"
#include "fxtk_desktop.h"
#include "fxtk_backends.h"   /* v2.4: 截图走 backends 的 PNG 编码 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "fxtk";

fx_widget_t s_root;
static fx_widget_t s_pool[FX_MAX_WIDGETS];
static const fx_driver_t *s_drv;
static fx_widget_t *s_pressed = NULL;
static fx_widget_t *s_parent = NULL;
static int s_touch_prev = 0;
static int s_last_tx = -1, s_last_ty = -1;
static int s_repaint = 1;
static int s_boot = 3;   /* 前3帧强制全屏, 杜绝偶发启动黑屏 */
static int s_full = 0;
static int s_autorepaint = 1;
/* 窗口默认背景: 245 灰白, 与 SDL 清屏一致, 无可见色差 */
static fx_color_t s_bg = FX_WINDOW_BG;
static int s_dark_theme = 0;
static fx_colorx_t s_global_bg = { FX_WINDOW_BG, FX_WINDOW_DARK };
#define FX_DIRTY_MAX 8
static int s_dirty[FX_DIRTY_MAX][4];
static int s_dirty_n = 0;
/* 绘制重入计数 (声明必须早于使用点: 立即重绘要用它防回调递归爆栈) */
static int s_draw_depth = 0;
static int s_tdbg_on = 0;
static char s_tdbg_str[48] = "touch: -";

/* 桌面扩展状态 */
static fx_widget_t *s_focus = NULL;
static fx_keyev_t s_last_key;
static int s_tick = 0, s_blink = 0;
static fx_widget_t *s_scroll_drag = NULL;
static fx_widget_t *s_wheel_tgt = NULL;
static int s_wheel_acc = 0;

/* 响应式缩放 */
#define FX_DESIGN_W 480
#define FX_DESIGN_H 272
static int s_sx1000 = 1000, s_sy1000 = 1000;

static void redraw_widget_now(fx_widget_t *w);
int fxtk_fps(void);
static int s_fpsdbg_on = 0;   /* 左下角 FPS 调试信息, 默认关 (demo 才开) */
void fxtk_set_fps_debug(int on) { s_fpsdbg_on = on ? 1 : 0; }
static void draw_debug_overlay(void)
{
    if (!s_drv || (!s_fpsdbg_on && !s_tdbg_on)) return;

    /* 调试文字覆盖在动画画布上时，先擦掉上一帧，避免残影闪烁。 */
    int y = (int)s_drv->height - 42;
    int x2 = s_drv->width > 220 ? 219 : (int)s_drv->width - 1;
    if (y < 0) y = 0;
    fx_reset_clip();
    fx_set_color(FX_BLACK);
    fx_fill_rect(0, y, x2, s_drv->height - 1);
    if (s_fpsdbg_on) {
        char b[48];
        snprintf(b, sizeof(b), "FPS:%d W:%d H:%d", fxtk_fps(), s_drv->width, s_drv->height);
        fx_draw_text_c(4, s_drv->height - 38, b, FX_GREEN, FX_BLACK);
    }
    if (s_tdbg_on)
        fx_draw_text_c(2, s_drv->height - 20, s_tdbg_str, FX_YELLOW, FX_BLACK);
}
static int te_len(fx_widget_t *w);
static int te_sel(fx_widget_t *w, int *a, int *b);
static void te_del_range2(fx_widget_t *w, int a, int b);
static void te_insert(fx_widget_t *w, const char *utf8);
static void te_backspace(fx_widget_t *w);
static void te_move(fx_widget_t *w, int key);
static void te_caret_from_xy(fx_widget_t *w, int x, int y);
static void te_caret_scroll(fx_widget_t *w);
static int te_chars_n(const char *s,int n);          /* v2.4.5: fx_set_title 改写文本框时要用 */
static int te_grow(fx_widget_t *w,int need);         /* v2.4.5: 同上 */
static int te_next_off(const char *s,int i);
static void te_move_vert(fx_widget_t *w,int dir);
static fx_widget_t *s_ctxpop,*s_ctx_te;
static int s_sel_mode=0;
static int s_ctx_open=0,s_ctx_hl=-1;
extern int fxtk_shift_down(void);
extern int fxtk_right_click(int*,int*);
static void ctx_do(int);
static void te_sel_word(fx_widget_t*);
static void te_sel_para(fx_widget_t*);
static const char *s_ctx_items[5];
static void ctx_draw(fx_widget_t*,void*);
static void ctx_draw_abs(void){ if(!s_ctxpop)return; int x1,y1,x2,y2; fx_widget_rect(s_ctxpop,&x1,&y1,&x2,&y2); int rh=(y2-y1)/5;
  fx_set_color(FX_WHITE); fx_fill_rect(x1,y1,x2,y2);
  fx_set_color(FX_GRAY); fx_draw_rect(x1,y1,x2,y2);
  for(int i=0;i<5;i++){ if(i==s_ctx_hl){fx_set_color(FX_RGB(33,150,243));fx_fill_rect(x1+1,y1+1+i*rh,x2-1,y1+1+i*rh+rh-1);}
    fxtk_draw_text_size(14,x1+6,y1+3+i*rh,s_ctx_items[i], i==s_ctx_hl?FX_WHITE:FX_RGB(40,40,40), i==s_ctx_hl?FX_RGB(33,150,243):FX_WHITE); } }
typedef struct { fx_widget_t *w; float off, tgt; int last; } fx_scroll_state_t;
#ifndef FX_MAX_SCROLL_STATES
/* v2.4.3: 并发滚动状态数。原先是硬编码 8(为 ESP32 省的), 但两端已分化 ——
 * PC 不必迁就 ESP 的资源约束, 否则用户多开几个带滚轮的列表就会遇到"这个列表滚不动了"这种人为 bug。
 * PC 默认 64(可用 -DFX_MAX_SCROLL_STATES=N 调), ESP32 仍保持 8; 池满会明确告警。 */
#  if defined(ESP_PLATFORM)
#    define FX_MAX_SCROLL_STATES 8
#  else
#    define FX_MAX_SCROLL_STATES 64
#  endif
#endif
static fx_scroll_state_t s_scroll_pool[FX_MAX_SCROLL_STATES];
/* v2.4.3: 控件池"高水位"。fxtk_alloc 线性找空槽 → 存活控件总是集中在前面,
 * 所以任何"遍历存活控件"的地方都应只扫到 high, 而不是扫满 FX_MAX_WIDGETS(PC 上 16384)。
 * 动画的每帧扫描就靠它把开销从 16384 次比较降到与存活数同阶。 */
static int s_pool_high = 0;   /* v2.3.1: unlink_free/fx_init 也要清它, 故随 typedef 前移至此 */
static fx_scroll_state_t *scroll_state(fx_widget_t *w);
static void scroll_drag_to(fx_widget_t *w, int y);
static void te_sel_para(fx_widget_t*);
static int te_word_start(const char*,int);
static int te_word_end(const char*,int);
static int te_para_start(const char*,int);
static int te_para_end(const char*,int);
static int widget_in_active_page(fx_widget_t *w)
{
    for (fx_widget_t *c = w; c->parent && c->parent != &s_root; c = c->parent)
        if (c->parent->type == FX_W_TAB)
            return c->page == c->parent->value;
    return 1;
}

/* ================= 控件池 ================= */
static int s_alloc_hint = 0;    /* v2.3: 分配游标 —— 旧实现每次都从 0 扫, 压测页每帧建控件时是 O(n²) */
static int s_widget_live = 0;   /* v2.3: 存活计数 —— 旧 fxtk_widget_count 每次扫 4096 项 (~40μs/次) */
fx_widget_t *fxtk_alloc(void)
{
    for (int k = 0; k < FX_MAX_WIDGETS; k++) {
        int i = (s_alloc_hint + k) % FX_MAX_WIDGETS;
        if (s_pool[i].type == FX_W_NONE) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_alloc_hint = i;
            s_widget_live++;
            if (i + 1 > s_pool_high) s_pool_high = i + 1;
        return &s_pool[i];
        }
    }
    fx_log(FX_LOG_ERROR, "控件池已满 (%d)! 加大 FX_MAX_WIDGETS 或复用控件", FX_MAX_WIDGETS);
    return NULL;
}
void fxtk_free(fx_widget_t *w)
{
    if (!w || w == &s_root || w->type == FX_W_NONE) return;
    s_widget_live--;
    w->type = FX_W_NONE;
}
void fxtk_link(fx_widget_t *parent, fx_widget_t *child)
{ child->parent = parent; child->sibling = parent->child; parent->child = child; }
void fx_parent(fx_widget_t *p) { s_parent = p; }
int fxtk_widget_count(void) { return s_widget_live; }
void fxtk_pool_reset_count(void) { s_widget_live = 0; s_alloc_hint = 0; }   /* fx_init 清池后同步 */

/* v2.4: 截图 —— 驱动回读当前帧, 统一走 backends 的 PNG 编码 (stb, 零额外依赖) */
int fx_screenshot(const char *path)
{
    if (!path || !path[0] || !s_drv || !s_drv->read_pixels) return 0;
    int w = (int)s_drv->width, h = (int)s_drv->height;
    if (w <= 0 || h <= 0) return 0;
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!px) return 0;
    if (!s_drv->read_pixels(px, w, h)) { free(px); return 0; }
    unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!rgba) { free(px); return 0; }
    for (int i = 0; i < w * h; i++) {
        uint32_t c = px[i];
        rgba[i * 4 + 0] = (unsigned char)((c >> 16) & 0xFF);
        rgba[i * 4 + 1] = (unsigned char)((c >> 8) & 0xFF);
        rgba[i * 4 + 2] = (unsigned char)(c & 0xFF);
        rgba[i * 4 + 3] = 255;
    }
    int ok = fx_img_save_png(path, rgba, w, h, 4);
    free(px);
    free(rgba);
    return ok;
}

/* ================= 属性构造器 ================= */
static int parse_xy(const char *s, int16_t *a, int16_t *b)
{ int x, y;
if (sscanf(s, "%d,%d", &x, &y) != 2) return 0; *a = (int16_t)x; *b = (int16_t)y; return 1; }
static int parse_pct(const char *s, int16_t *a, int16_t *b)
{ float x, y;
if (sscanf(s, "%f,%f", &x, &y) != 2) return 0;
/* v2.3.1: percent() 收 0~1 小数; 误写 "100,100" 会在 int16 里回绕成负数把控件甩出屏幕, 钳到合法域 */
if (x < -1.0f) x = -1.0f;
if (x > 1.0f) x = 1.0f;
if (y < -1.0f) y = -1.0f;
if (y > 1.0f) y = 1.0f;
*a = (int)(x*1000); *b = (int)(y*1000); return 1; }
fx_attr_t pixel(const char *a, const char *b)
{ fx_attr_t at = { FX_A_PIXEL, { {0} } }; parse_xy(a,&at.v.rect.x1,&at.v.rect.y1); parse_xy(b,&at.v.rect.x2,&at.v.rect.y2); return at; }
fx_attr_t percent(const char *a, const char *b)
{ fx_attr_t at = { FX_A_PERCENT, { {0} } }; parse_pct(a,&at.v.pct.p1,&at.v.pct.p2); parse_pct(b,&at.v.pct.p3,&at.v.pct.p4); return at; }
fx_attr_t grid1(const char *g) { fx_attr_t a = { FX_A_GRID, { {0} } }; a.v.grid.name = g; return a; }
fx_attr_t grid5(const char *g, int r1, int c1, int r2, int c2)
{ fx_attr_t a = { FX_A_GRID, { {0} } }; a.v.grid.name=g; a.v.grid.r1=(int16_t)r1; a.v.grid.c1=(int16_t)c1; a.v.grid.r2=(int16_t)r2; a.v.grid.c2=(int16_t)c2; return a; }
fx_attr_t title(const char *s)  { fx_attr_t a={FX_A_TITLE,{ {0} }}; a.v.str.s=s; return a; }
fx_attr_t text(const char *s)   { fx_attr_t a={FX_A_TITLE,{ {0} }}; a.v.str.s=s; return a; }
fx_attr_t name(const char *s)   { fx_attr_t a={FX_A_NAME,{ {0} }}; a.v.str.s=s; return a; }
fx_attr_t call(fx_cb_t cb)      { fx_attr_t a={FX_A_CALL,{ {0} }}; a.v.cb.cb=cb; return a; }
fx_attr_t line(int n)           { fx_attr_t a={FX_A_LINE,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t row(int n)            { fx_attr_t a={FX_A_ROW,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t color(fx_color_t c)   { fx_attr_t a={FX_A_COLOR,{ {0} }}; a.v.color.c=c; return a; }
fx_attr_t fgcolor(fx_color_t c) { fx_attr_t a={FX_A_FGCOLOR,{ {0} }}; a.v.color.c=c; return a; }
fx_attr_t dense(void) { fx_attr_t a={FX_A_DENSE,{ {0} }}; return a; }
fx_attr_t border(int n)         { fx_attr_t a={FX_A_BORDER,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t radius(int n)         { fx_attr_t a={FX_A_RADIUS,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t value(int n)          { fx_attr_t a={FX_A_VALUE,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t page(int n)           { fx_attr_t a={FX_A_PAGE,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
/* v2.4.3: 全局动画开关。默认关 —— 无头测试与金图回归必须确定性, 不能每帧都在动。 */
static int s_anim_on = 0;
void fx_animation(int on) { s_anim_on = on ? 1 : 0; fx_repaint(); }
int  fx_animation_enabled(void) { return s_anim_on; }
int  fx_widget_anim_ok(fx_widget_t *w) { return s_anim_on && w && !(w->flags & FX_F_NOANIM); }

fx_attr_t anim(int n)           { fx_attr_t a={FX_A_ANIM,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t sidebar(int side)     { fx_attr_t a={FX_A_SIDEBAR,{ {0} }}; a.v.iv.v=(int16_t)side; return a; }
fx_attr_t fx_wptr(fx_widget_t *w){ fx_attr_t a={FX_A_WIDGET,{ {0} }}; a.v.w.w=w; return a; }
fx_attr_t maxlen(int n)         { fx_attr_t a={FX_A_MAXLEN,{ {0} }}; a.v.iv.v=(int16_t)n; return a; }
fx_attr_t image(fx_image_t *img){ fx_attr_t a={FX_A_IMAGE,{ {0} }}; a.v.w.w=(fx_widget_t*)img; return a; }

/* ================= 控件创建 ================= */
fx_widget_t *fx_widget_new_impl(int type, fx_attr_t attrs[])
{
    fx_widget_t *w = fxtk_alloc();
    if (!w) return NULL;
    w->type = (uint8_t)type; w->flags = FX_F_VISIBLE; w->pos_mode = FX_POS_PIXEL;
    w->border = 1; w->radius = 4; w->fg = FX_RGB(40, 40, 40);   /* 默认深字 */
    switch (type) {
    case FX_W_LABEL: w->bg = FX_BLACK; break;                            /* 黑=哨兵, 透明用窗口背景 */
    case FX_W_CANVAS: w->bg = FX_RGB(245, 245, 245); break;              /* 浅底画布, 回调自行铺底 */
    case FX_W_CHECKBOX: w->bg = FX_BLACK; break;
    case FX_W_IMAGE: w->bg = FX_BLACK; break;                            /* 图片透明底 */
    case FX_W_GRID: case FX_W_PANEL: case FX_W_TAB: case FX_W_SCROLL: w->bg = FX_RGB(245, 245, 245); w->fg = FX_LGRAY; break;  /* 浅底+浅网格线 */
    case FX_W_SLIDER: w->bg = FX_TOK_SUCCESS; w->fg = FX_TOK_MUTED; break;   /* 绿色填充, 浅灰轨道 */
    case FX_W_PROGRESS: w->bg = FX_TOK_SUCCESS; w->fg = FX_TOK_MUTED; break;
    case FX_W_TEXTEDIT: w->bg = FX_BLACK; w->fg = FX_TOK_TEXT; break;  /* 哨兵→绘制时白底黑字 */
    case FX_W_BUTTON: w->bg = FX_TOK_PRIMARY; w->fg = FX_TOK_ON_PRIMARY; break;  /* 蓝底白字 */
    default: w->bg = FX_TOK_PRIMARY; w->fg = FX_TOK_ON_PRIMARY; break;
    }
    for (int i = 0; attrs[i].tag != FX_A_NONE; i++) {
        switch (attrs[i].tag) {
        case FX_A_PIXEL: w->pos_mode=FX_POS_PIXEL; w->ox1=attrs[i].v.rect.x1; w->oy1=attrs[i].v.rect.y1; w->ox2=attrs[i].v.rect.x2; w->oy2=attrs[i].v.rect.y2; break;
        case FX_A_PERCENT: w->pos_mode=FX_POS_PERCENT; w->px1=attrs[i].v.pct.p1; w->py1=attrs[i].v.pct.p2; w->px2=attrs[i].v.pct.p3; w->py2=attrs[i].v.pct.p4; break;
        case FX_A_GRID: w->pos_mode=FX_POS_GRID; w->grid_ref=fx_find(attrs[i].v.grid.name); w->gname=attrs[i].v.grid.name; w->gr1=attrs[i].v.grid.r1; w->gc1=attrs[i].v.grid.c1; w->gr2=attrs[i].v.grid.r2; w->gc2=attrs[i].v.grid.c2; break;
        case FX_A_TITLE: { const char *ts0 = attrs[i].v.str.s?attrs[i].v.str.s:"";
            size_t tl0 = strlen(ts0);
            if (tl0 > sizeof(w->title)-1) tl0 = sizeof(w->title)-1;
            while (tl0 > 0 && ((unsigned char)ts0[tl0] & 0xC0) == 0x80) tl0--;   /* v2.3.1: UTF-8 边界回退 */
            memcpy(w->title, ts0, tl0); w->title[tl0]=0; break; }
        case FX_A_NAME: strncpy(w->name, attrs[i].v.str.s?attrs[i].v.str.s:"", sizeof(w->name)-1); w->name[sizeof(w->name)-1]=0; break;
        case FX_A_CALL: w->cb=attrs[i].v.cb.cb; break;
        case FX_A_LINE: w->lines=attrs[i].v.iv.v; break;
        case FX_A_ROW: w->rows=attrs[i].v.iv.v; break;
        case FX_A_COLOR: w->bg=attrs[i].v.color.c; break;
        case FX_A_FGCOLOR: w->fg=attrs[i].v.color.c; break;
        case FX_A_DENSE: w->flags|=FX_F_DENSE; break;
        case FX_A_BORDER: w->border=(uint8_t)attrs[i].v.iv.v; break;
        case FX_A_RADIUS: w->radius=(uint8_t)attrs[i].v.iv.v; break;
        case FX_A_VALUE: w->value=attrs[i].v.iv.v; break;
        case FX_A_PAGE: w->page=attrs[i].v.iv.v; break;
        case FX_A_IMAGE: w->img=(fx_image_t*)attrs[i].v.w.w; break;
        case FX_A_MAXLEN: w->text_max=attrs[i].v.iv.v; break;
        case FX_A_ANIM: if (attrs[i].v.iv.v) w->flags|=FX_F_ANIM; else w->flags|=FX_F_NOANIM; break;   /* v2.4.3: anim(0)=显式关动画 */
        case FX_A_SIDEBAR: w->tab_side=(uint8_t)attrs[i].v.iv.v; break;
        default: break;
        }
    }
    fxtk_link(w->grid_ref ? w->grid_ref : (s_parent ? s_parent : &s_root), w);
    if (type == FX_W_TEXTEDIT) {
        w->text_cap = 128; w->text_buf = (char*)malloc(128);
        if (w->text_buf) { memcpy(w->text_buf, w->title, 127); w->text_buf[127]=0; }
        w->caret = w->anchor = (int)strlen(w->text_buf ? w->text_buf : "");
    }
    if (type == FX_W_TAB) { int n=1; 
    for(const char *p=w->title; *p; p++) if (*p==',') n++; w->lines=(int16_t)n; }
    fx_layout(); fx_repaint();
    if (type == FX_W_CANVAS && fxtk_aa()) fx_canvas_enable_buf(w);   /* v2.2: 开抗锯齿的画布自动离屏, 以便边缘混合 */
    return w;
}

fx_widget_t *fx_find(const char *n)
{
    if (!n) return NULL;
    for (int i = 0; i < FX_MAX_WIDGETS; i++)
        if (s_pool[i].type != FX_W_NONE && s_pool[i].name[0] && strcmp(s_pool[i].name, n)==0) return &s_pool[i];
    return NULL;
}
static void unlink_free(fx_widget_t *w)
{
    if (!w || w == &s_root) return;
    if (w->parent) { fx_widget_t **pp=&w->parent->child; 
    while(*pp && *pp!=w) pp=&(*pp)->sibling;
    if (*pp) *pp=w->sibling; }
    while (w->child) unlink_free(w->child);
    if (s_pressed==w) s_pressed=NULL;
    if (s_focus==w) s_focus=NULL;
    if (s_scroll_drag==w) s_scroll_drag=NULL;
    if (s_wheel_tgt==w) s_wheel_tgt=NULL;      /* v2.3.1: 补齐悬垂清理 */
    if (s_ctx_te==w) s_ctx_te=NULL;
    if (s_ctxpop==w) { s_ctxpop=NULL; s_ctx_open=0; }
    /* v2.3.1: 池地址复用会让新控件继承旧状态, 一并回收 */
    for (int i = 0; i < FX_MAX_SCROLL_STATES; i++)
        if (s_scroll_pool[i].w == w) memset(&s_scroll_pool[i], 0, sizeof(s_scroll_pool[i]));
    fxtk_extra_forget(w);   /* v2.3.1: 清 list/drop 模块按指针索引的槽位 (防池复用悬垂) */
    if (w->text_buf) { free(w->text_buf); w->text_buf=NULL; }
    if (w->offbuf) { free(w->offbuf); w->offbuf=NULL; }
    fxtk_free(w);
}
void fx_delete_impl(fx_attr_t attrs[])
{
    for (int i=0; attrs[i].tag!=FX_A_NONE; i++) {
        fx_widget_t *w = attrs[i].tag==FX_A_GRID ? fx_find(attrs[i].v.grid.name) : (attrs[i].tag==FX_A_WIDGET ? attrs[i].v.w.w : NULL);
        if (w) unlink_free(w);
    }
    fx_layout(); fx_repaint();
}

/* ================= 布局(响应式) ================= */

static int s_maxscale1000 = 1600;   /* 控件最大=1.6x设计尺寸, 大屏自适应不再放大 */
void fxtk_fit_rect(fx_widget_t *w,int*x1,int*y1,int*x2,int*y2){
    if(!(w->flags&FX_F_FIT))return;
    if (s_sx1000<=1000&&s_sy1000<=1000)return;
    int cw=*x2-*x1+1,ch=*y2-*y1+1;
    int csx=s_sx1000>s_maxscale1000?s_maxscale1000:s_sx1000;
    int csy=s_sy1000>s_maxscale1000?s_maxscale1000:s_sy1000;
    int desw=(int)((int64_t)cw*1000/s_sx1000),desh=(int)((int64_t)ch*1000/s_sy1000);
    int nw=(int)((int64_t)desw*csx/1000),nh=(int)((int64_t)desh*csy/1000);
    if (nw>=cw&&nh>=ch)return;
    int cx=(*x1+*x2)/2,cy=(*y1+*y2)/2;
    *x1=cx-nw/2;*x2=*x1+nw-1;*y1=cy-nh/2;*y2=*y1+nh-1;
}
void fx_set_fit(fx_widget_t *w,int on){ if(!w)return;
if (on)w->flags|=FX_F_FIT; else w->flags&=~FX_F_FIT; fx_layout(); }
int fxtk_max_scale1000(void){return s_maxscale1000;}
void fx_set_max_scale(float f){ if(f<0.5f)f=0.5f;
if (f>8)f=8; s_maxscale1000=(int)(f*1000); fx_layout(); }
/* 交互件尺寸封顶+居中: 设计分辨率下不生效, 大屏防巨型/grid拉满 */
static void clamp_ctrl(fx_widget_t *c){
    if (c->type!=FX_W_BUTTON && c->type!=FX_W_CHECKBOX && !(c->flags&FX_F_FIT)) return;
    if (c->grid_ref && (c->grid_ref->flags & FX_F_DENSE)) return;   /* 密集: 铺满单元 */
    if (s_sx1000<=1000 && s_sy1000<=1000) return;
    int csx = s_sx1000>s_maxscale1000?s_maxscale1000:s_sx1000;
    int csy = s_sy1000>s_maxscale1000?s_maxscale1000:s_sy1000;
    int raww=c->x2-c->x1+1, rawh=c->y2-c->y1+1;
    int desw, desh;
    if (c->pos_mode==FX_POS_PIXEL){ desw=c->ox2-c->ox1+1; desh=c->oy2-c->oy1+1; }
    else { desw=(int)((int64_t)raww*1000/s_sx1000); desh=(int)((int64_t)rawh*1000/s_sy1000); }
    int neww=(int)((int64_t)desw*csx/1000), newh=(int)((int64_t)desh*csy/1000);
    if (neww>=raww && newh>=rawh) return;
    int cx=(c->x1+c->x2)/2, cy=(c->y1+c->y2)/2;
    c->x1=(int16_t)(cx-neww/2); c->x2=(int16_t)(c->x1+neww-1);
    c->y1=(int16_t)(cy-newh/2); c->y2=(int16_t)(c->y1+newh-1);
}
void fxtk_apply_fit(fx_widget_t *w){   /* 绘制期幂等fit: 从父+设计坐标现算, 无反馈 */
    if(!(w->flags&FX_F_FIT) || w->pos_mode!=FX_POS_PIXEL) return;
    if (s_sx1000<=1000 && s_sy1000<=1000) return;
    fx_widget_t *p = w->parent ? w->parent : &s_root;
    int rx1=p->x1+(int32_t)w->ox1*s_sx1000/1000, ry1=p->y1+(int32_t)w->oy1*s_sy1000/1000;
    int rx2=p->x1+(int32_t)w->ox2*s_sx1000/1000, ry2=p->y1+(int32_t)w->oy2*s_sy1000/1000;
    int csx=s_sx1000>s_maxscale1000?s_maxscale1000:s_sx1000;
    int csy=s_sy1000>s_maxscale1000?s_maxscale1000:s_sy1000;
    int nw=(int)((int64_t)(w->ox2-w->ox1+1)*csx/1000), nh=(int)((int64_t)(w->oy2-w->oy1+1)*csy/1000);
    int cx=(rx1+rx2)/2, cy=(ry1+ry2)/2;
    w->x1=(int16_t)(cx-nw/2); w->x2=(int16_t)(w->x1+nw-1);
    w->y1=(int16_t)(cy-nh/2); w->y2=(int16_t)(w->y1+nh-1);
}
static void layout_children(fx_widget_t *p);
void fx_layout(void)
{
    if (!s_drv) return;
    s_root.x1=0; s_root.y1=0; s_root.x2=(int16_t)(s_drv->width-1); s_root.y2=(int16_t)(s_drv->height-1);
    s_sx1000 = (int)((int32_t)s_drv->width*1000/FX_DESIGN_W);
    s_sy1000 = (int)((int32_t)s_drv->height*1000/FX_DESIGN_H);
    layout_children(&s_root);
}
static void layout_children(fx_widget_t *p)
{
    for (fx_widget_t *c=p->child; c; c=c->sibling) {
        if (!(c->flags & FX_F_VISIBLE)) continue;
        switch (c->pos_mode) {
        case FX_POS_PIXEL:
            c->x1=(int16_t)(p->x1+(int32_t)c->ox1*s_sx1000/1000); c->y1=(int16_t)(p->y1+(int32_t)c->oy1*s_sy1000/1000);
            c->x2=(int16_t)(p->x1+(int32_t)c->ox2*s_sx1000/1000); c->y2=(int16_t)(p->y1+(int32_t)c->oy2*s_sy1000/1000);
            break;
        case FX_POS_PERCENT: {
            int pw=p->x2-p->x1+1, ph=p->y2-p->y1+1;
            c->x1=(int16_t)(p->x1+(int32_t)pw*c->px1/1000); c->y1=(int16_t)(p->y1+(int32_t)ph*c->py1/1000);
            c->x2=(int16_t)(p->x1+(int32_t)pw*c->px2/1000-1); c->y2=(int16_t)(p->y1+(int32_t)ph*c->py2/1000-1);
            break; }
        case FX_POS_FIXED: break;   /* 弹层: 保留直写坐标 */
        case FX_POS_GRID: {
            fx_widget_t *g=c->grid_ref;
            /* v2.3.1: grid 晚于子件创建时布局期重解析 (旧逻辑只在创建瞬间解析一次,
             * 先子后父/名字打错 → 子件永远停在 (0,0,0,0) 且无任何提示) */
            if (!g && c->gname) {
                g = c->grid_ref = fx_find(c->gname);
                if (!g) fx_log(FX_LOG_WARN, "grid '%s' 不存在(控件 '%s')", c->gname, c->name);
            }
            if (g && g->lines>0 && g->rows>0) {
                int cw=(g->x2-g->x1+1)/g->rows, ch=(g->y2-g->y1+1)/g->lines;
                c->x1=(int16_t)(g->x1+(c->gc1-1)*cw); c->y1=(int16_t)(g->y1+(c->gr1-1)*ch);
                c->x2=(int16_t)(g->x1+c->gc2*cw-1); c->y2=(int16_t)(g->y1+c->gr2*ch-1);
            } break; }
        }
        clamp_ctrl(c);
        layout_children(c);
    }
}

/* ================= 命中测试 ================= */
static fx_widget_t *hit_test(fx_widget_t *w, int x, int y)
{
    fx_widget_t *r = NULL;
    if (w->type == FX_W_SCROLL) {
        /* v2.3.1: ①容器自身隐藏 → 整体不可命中; ②触点不在视口内直接拒绝
         * (旧逻辑对滚动框外的任何点击都按 +scroll_y 映射进内容坐标, 会命中
         * 滚出去的"看不见的"子件); ③子件检查自身可见性, 与通用分支一致 */
        if (!(w->flags & FX_F_VISIBLE)) return NULL;
        if (x < w->x1 || x > w->x2 || y < w->y1 || y > w->y2) return NULL;
        for (fx_widget_t *c=w->child; c && !r; c=c->sibling)
            if (c->flags & FX_F_VISIBLE) r = hit_test(c, x, y + w->scroll_y);
        return r ? r : w;
    }
    if (w->type == FX_W_TAB) {
        /* 命中标签条: 侧边栏方位决定判定区域 */
        switch (w->tab_side) {
        case FX_TAB_LEFT:  if (x>=w->x1 && x<=w->x1+FX_TAB_SIDE-1 && y>=w->y1 && y<=w->y2) return w; break;
        case FX_TAB_RIGHT: if (x>=w->x2-FX_TAB_SIDE+1 && x<=w->x2 && y>=w->y1 && y<=w->y2) return w; break;
        case FX_TAB_BOTTOM:if (y>=w->y2-FX_TAB_H+1 && y<=w->y2 && x>=w->x1 && x<=w->x2) return w; break;
        default: /* top */
            if (y>=w->y1 && y<=w->y1+FX_TAB_H-1 && x>=w->x1 && x<=w->x2) return w;
        }
        for (fx_widget_t *c=w->child; c && !r; c=c->sibling)
            if (c->page==w->value && (c->flags & FX_F_VISIBLE)) r = hit_test(c, x, y);
        return r;
    }
    for (fx_widget_t *c=w->child; c && !r; c=c->sibling)
        if (c->flags & FX_F_VISIBLE) r = hit_test(c, x, y);
        if (r) return r;
        if (!(w->flags & FX_F_VISIBLE) || w==&s_root) return NULL;
        if (w->type==FX_W_GRID || w->type==FX_W_PANEL || w->type==FX_W_SCROLL) return NULL;
        if (x>=w->x1 && x<=w->x2 && y>=w->y1 && y<=w->y2) return w;
    return NULL;
}

/* ================= 触摸事件 ================= */
void fx_touch_press(int x, int y)
{
    if(s_ctx_open&&s_ctxpop){ int x1,y1,x2,y2; fx_widget_rect(s_ctxpop,&x1,&y1,&x2,&y2);
    if (x>=x1&&x<=x2&&y>=y1&&y<=y2){ int rh=(y2-y1)/5; int idx=(y-y1)/rh;
    if (idx<0)idx=0;
    if (idx>4)idx=4; ctx_do(idx); } else { fx_set_visible(s_ctxpop,0); s_ctx_open=0; fx_repaint(); }
      return; }

    s_pressed = hit_test(&s_root, x, y);
    if (s_pressed && (s_pressed->type==FX_W_TEXTEDIT || s_pressed->type==FX_W_SCROLL || s_pressed->type==FX_W_CANVAS)
        && s_pressed->content_h > (s_pressed->y2 - s_pressed->y1)
        && x >= s_pressed->x2 - 6) {
        s_scroll_drag = s_pressed; scroll_drag_to(s_pressed, y); return;
    }
    if (s_pressed && s_pressed->type == FX_W_TEXTEDIT) {
        fx_set_focus(s_pressed);
        if (fxtk_shift_down()) { te_caret_from_xy(s_pressed,x,y); }   /* Shift+点击=扩展 */
        else { te_caret_from_xy(s_pressed, x, y);
          static int lx=-1,ly=-1,lt=0,cn=0;
          if ((x-lx)*(x-lx)+(y-ly)*(y-ly)<=16&&(s_tick-lt)<45)cn++; else cn=1; lx=x;ly=y;lt=s_tick;
          if (cn==2){s_sel_mode=1;te_sel_word(s_pressed);} else if(cn>=3){s_sel_mode=2;te_sel_para(s_pressed);cn=0;} else {s_sel_mode=0;s_pressed->anchor=s_pressed->caret;} }
    } else if (s_pressed) fx_set_focus(NULL);
    if (s_pressed) { s_pressed->flags |= FX_F_PRESSED; redraw_widget_now(s_pressed); }
}
void fx_touch_release(int x, int y)
{
    fx_widget_t *w = hit_test(&s_root, x, y);
    fx_widget_t *p = s_pressed;
    s_pressed = NULL; s_scroll_drag = NULL;
    if (p) {
        p->flags &= (uint8_t)~FX_F_PRESSED;
        redraw_widget_now(p);
        if (w == p) {
            if (p->type == FX_W_TAB) {
                int n = p->lines>0?p->lines:1, pg=0;
                /* 侧边栏方位决定按哪一维算页码 */
                if (p->tab_side==FX_TAB_LEFT || p->tab_side==FX_TAB_RIGHT) {
                    int th=(p->y2-p->y1+1)/n;
                    if (th>0) pg=(y-p->y1)/th;
                } else {
                    int tw=(p->x2-p->x1+1)/n;
                    if (tw>0) pg=(x-p->x1)/tw;
                }
                if (pg<0) pg=0;
                if (pg>=n) pg=n-1;
                if (pg != p->value) { p->value=(int16_t)pg; fx_repaint_rect(p->x1,p->y1,p->x2,p->y2); }
                if (s_focus && !widget_in_active_page(s_focus)) s_focus = NULL;
            } else {
                if (p->type == FX_W_CHECKBOX) p->value = !p->value;
                if (p->cb && p->type != FX_W_CANVAS) p->cb(p, p->ud);
            }
        }
    }
}
void fx_touch_move(int x, int y)
{
    if(s_ctx_open&&s_ctxpop){ int x1,y1,x2,y2; fx_widget_rect(s_ctxpop,&x1,&y1,&x2,&y2);
      int hl=-1;
      if (x>=x1&&x<=x2&&y>=y1&&y<=y2){ hl=(y-y1)/((y2-y1)/5);
      if (hl<0)hl=0;
      if (hl>4)hl=4; }
      if(hl!=s_ctx_hl){ s_ctx_hl=hl; fx_repaint(); } return; }

    if (s_scroll_drag) { scroll_drag_to(s_scroll_drag, y); return; }
    if (s_pressed && s_pressed->type == FX_W_TEXTEDIT) {
fx_widget_t *te=s_pressed;
int vis=te->y2-te->y1-10, maxs=te->content_h-vis;
if (maxs<0)maxs=0;
if (y>te->y2) te->scroll_y+=(int16_t)((y-te->y2)>10?10:(y-te->y2));      /* 拖出下缘=自动下滚 */
else if (y<te->y1) te->scroll_y-=(int16_t)((te->y1-y)>10?10:(te->y1-y)); /* 拖出上缘=自动上滚 */
if(te->scroll_y<0)te->scroll_y=0;
if (te->scroll_y>maxs)te->scroll_y=(int16_t)maxs;
te_caret_from_xy(te,x,y); int ci=te->caret;
if (s_sel_mode==1){ te->caret=(ci>=te->anchor)?te_word_end(te->text_buf,ci):te_word_start(te->text_buf,ci); }
else if(s_sel_mode==2){ te->caret=(ci>=te->anchor)?te_para_end(te->text_buf,ci):te_para_start(te->text_buf,ci); }
redraw_widget_now(te); return; }
    if (s_pressed && s_pressed->type == FX_W_SLIDER) {
        int w = s_pressed->x2 - s_pressed->x1 + 1;
        int v = w>0 ? (x-s_pressed->x1)*100/w : 0;
        if (v<0)v=0;
        if (v>100)v=100;
        if (v != s_pressed->value) { s_pressed->value=(int16_t)v; redraw_widget_now(s_pressed);
        if (s_pressed->cb) s_pressed->cb(s_pressed,s_pressed->ud); }
    }
}

/* v2.4.3 动画: 切页"揭示"过渡。
 * 思路: 不改控件几何(那会牵动布局与命中测试), 而是在换页后的 160ms 内, 把 tab 子控件的裁剪矩形
 * 从一侧逐步展开 —— 新页面像被"拉出来"一样出现。方向跟新标签所在的一侧一致(往右切就从右侧展开)。
 * 状态检测靠"记住上一帧的 value", 因此完全不碰输入路径(改动面最小)。 */
#define FX_PAGE_WIPE_MS 220u
typedef struct { fx_widget_t *w; int prev; uint32_t t0; } fx_wipe_t;
#define FX_WIPE_SLOTS 8   /* v2.4.4: 原先散落 4 处硬编码 8 */
static fx_wipe_t s_wipe[FX_WIPE_SLOTS];
static float fx_page_wipe(fx_widget_t *w)   /* 返回揭示进度 0..1(1=无动画) */
{
    if (!fx_widget_anim_ok(w)) return 1.0f;
    int k = -1;
    for (int i = 0; i < FX_WIPE_SLOTS; i++) if (s_wipe[i].w == w) { k = i; break; }
    if (k < 0) for (int i = 0; i < FX_WIPE_SLOTS; i++) if (!s_wipe[i].w) { s_wipe[i].w = w; s_wipe[i].prev = w->value; s_wipe[i].t0 = (uint32_t)fx_time_ms(); k = i; break; }
    if (k < 0) return 1.0f;
    if (s_wipe[k].prev != w->value) { s_wipe[k].prev = w->value; s_wipe[k].t0 = (uint32_t)fx_time_ms(); }
    uint32_t el = (uint32_t)fx_time_ms() - s_wipe[k].t0;
    if (el >= FX_PAGE_WIPE_MS) { w->flags &= (uint16_t)~FX_F_ANIM; return 1.0f; }
    w->flags |= FX_F_ANIM;
    { float t = (float)el / (float)FX_PAGE_WIPE_MS; float u = 1.0f - t; return 1.0f - u * u * u; }   /* 缓出 */
}
/* 把 tab 子控件的裁剪收窄到"已揭示"的部分 */
static void fx_page_wipe_clip(fx_widget_t *w, int x1, int y1, int x2, int y2, float p, int *ox1, int *oy1, int *ox2, int *oy2)
{
    *ox1 = x1; *oy1 = y1; *ox2 = x2; *oy2 = y2;
    if (p >= 1.0f) return;
    int wpx = (x2 - x1 + 1);
    int span = (int)(wpx * p);
    int k = -1;
    for (int i = 0; i < FX_WIPE_SLOTS; i++) if (s_wipe[i].w == w) { k = i; break; }
    int from_left = (k >= 0 && s_wipe[k].prev <= w->value);
    if (from_left) *ox1 = x1 + (wpx - span);            /* 从右侧展开(往右切页) */
    else           { *ox2 = x1 + span - 1; }            /* 从左侧展开(往左切页) */
    if (*ox2 < *ox1) *ox2 = *ox1;
}

static void redraw_region(int x1,int y1,int x2,int y2);   /* 定义在后面: 立即重绘复用它擦背景 */

static void redraw_widget_now(fx_widget_t *w)
{
    if (!w || !(w->flags & FX_F_VISIBLE)) return;
    if (!widget_in_active_page(w)) return;
    switch (w->type) {
#if FXTK_WIDGET_BUTTON || FXTK_WIDGET_SLIDER || FXTK_WIDGET_PROGRESS || FXTK_WIDGET_CHECKBOX
    case FX_W_BUTTON: case FX_W_SLIDER: case FX_W_PROGRESS: case FX_W_CHECKBOX:
        /* 【v2.4 修复】立即重绘过去是"只设裁剪、直接画", 完全不擦旧像素 —— 于是拖动滑杆
         * 会沿路留下一串滑块残影(用户报的"脏区未清除"就是这个; 文本光标/进度条同理)。
         * 现在与脏区路径 redraw_region 完全同语义: 先用窗口背景铺满该矩形, 再重画所有与之
         * 相交的控件(容器自己会铺自己的底色), 所以放在彩色卡片上的控件也不会被凿出洞。 */
        if (s_draw_depth > 0) {                 /* 正在绘制 → 只标脏, 防回调递归爆栈 */
            fx_repaint_rect(w->x1, w->y1, w->x2, w->y2);
            return;
        }
        redraw_region(w->x1, w->y1, w->x2, w->y2);
        return;
#endif
#if FXTK_WIDGET_CANVAS
    case FX_W_CANVAS:
        fx_set_clip(w->x1,w->y1,w->x2,w->y2);
        if ((w->flags & FX_F_BUF) && w->cb) { fxtk_off_begin(w); fxtk_draw_canvas(w); fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); fxtk_off_end(w); }
        else { fxtk_draw_canvas(w);
        if (w->cb) { fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); } }
        fx_reset_clip(); return;
#endif
    default: fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
    }
}

/* ================= 绘制 ================= */
/* 重入计数: 绘制期间若控件回调又要求"立即重绘", 只标脏交给本帧统一重绘 ——
 * 否则会形成 draw_widget → redraw_region → redraw_widget_now → 控件回调(fx_set_value) → draw_widget
 * 的无限递归, 直接压爆线程栈(压测页 1280x720 实测必崩, Wine 下报的就是 stack overflow)。 */
static void draw_widget_inner(fx_widget_t *w, int cx1, int cy1, int cx2, int cy2);
static void draw_widget(fx_widget_t *w, int cx1, int cy1, int cx2, int cy2)
{
    s_draw_depth++;
    draw_widget_inner(w, cx1, cy1, cx2, cy2);
    s_draw_depth--;
}
static void draw_widget_inner(fx_widget_t *w, int cx1, int cy1, int cx2, int cy2)
{
    if (!(w->flags & FX_F_VISIBLE)) return;
    int x1=w->x1>cx1?w->x1:cx1, y1=w->y1>cy1?w->y1:cy1, x2=w->x2<cx2?w->x2:cx2, y2=w->y2<cy2?w->y2:cy2;
    if (x1<=x2 && y1<=y2) {
        fx_set_clip(x1,y1,x2,y2);
        switch (w->type) {
#if FXTK_WIDGET_TAB
        case FX_W_TAB:
            fxtk_draw_tab(w);
            {
                int c1 = w->x1, c2 = w->y1, c3 = w->x2, c4 = w->y2;
                float _p = fx_page_wipe(w);
                if (_p < 1.0f) fx_page_wipe_clip(w, w->x1, w->y1, w->x2, w->y2, _p, &c1, &c2, &c3, &c4);
                /* v2.4.3: 收窄后的矩形必须【当作裁剪参数传进去】—— draw_widget 的第 2~5 个参数就是裁剪矩形,
                 * 之前在外面 fx_set_clip 会被它覆盖, 所以换页过渡一直看不到效果。 */
                for (fx_widget_t *c=w->child; c; c=c->sibling) if (c->page==w->value) draw_widget(c, c1, c2, c3, c4);
            }
            return;
#endif
        case FX_W_SCROLL: {
            fx_set_color(w->bg); fx_fill_rect(w->x1,w->y1,w->x2,w->y2);
            int dy = -w->scroll_y;
            for (fx_widget_t *c=w->child; c; c=c->sibling) {
                c->y1+=(int16_t)dy; c->y2+=(int16_t)dy;
                draw_widget(c,cx1,cy1+dy,cx2,cy2+dy);
                c->y1-=(int16_t)dy; c->y2-=(int16_t)dy;   /* 必须还原 */
            }
            int vis2=y2-y1+1, tot2=w->content_h;
            if (tot2>vis2) {
                int th=vis2*vis2/tot2;
                if (th<20) th=20;
                int ty=y1+(int)((long)w->scroll_y*(vis2-th)/(tot2-vis2));
                fx_set_color(FX_GRAY);  fx_fill_rect(x2-4,y1+2,x2-1,y2-2);
                fx_set_color(FX_LGRAY); fx_fill_rect(x2-4,ty,x2-1,ty+th);
            }
            return; }
#if FXTK_WIDGET_BUTTON
        case FX_W_BUTTON: fxtk_draw_button(w); break;
#endif
#if FXTK_WIDGET_LABEL
        case FX_W_LABEL: fxtk_draw_label(w); break;
#endif
#if FXTK_WIDGET_GRID
        case FX_W_GRID: fxtk_draw_grid(w); break;
#endif
#if FXTK_WIDGET_PANEL
        case FX_W_PANEL: fxtk_draw_panel(w); break;
#endif
#if FXTK_WIDGET_SLIDER
        case FX_W_SLIDER: fxtk_draw_slider(w); break;
#endif
#if FXTK_WIDGET_PROGRESS
        case FX_W_PROGRESS: fxtk_draw_progress(w); break;
#endif
#if FXTK_WIDGET_CHECKBOX
        case FX_W_CHECKBOX: fxtk_draw_checkbox(w); break;
#endif
#if FXTK_WIDGET_TEXTEDIT
        case FX_W_TEXTEDIT: fxtk_draw_textedit(w); break;
#endif
#if FXTK_WIDGET_IMAGE
        case FX_W_IMAGE: fxtk_draw_image(w); break;
#endif
#if FXTK_WIDGET_CANVAS
        case FX_W_CANVAS:
            if ((w->flags & FX_F_BUF) && w->cb) { fxtk_off_begin(w); fxtk_draw_canvas(w); fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); fxtk_off_end(w); }
            else { fxtk_draw_canvas(w);
            if (w->cb) { fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); } }
            break;
#endif
        default: break;
        }
    }
    for (fx_widget_t *c=w->child; c; c=c->sibling) draw_widget(c,cx1,cy1,cx2,cy2);
}
void fxtk_draw_all(void)
{
    if (!s_drv) return;
    fx_set_color(s_bg); fx_fill_rect(0,0,s_drv->width-1,s_drv->height-1);
    draw_widget(&s_root,0,0,s_drv->width-1,s_drv->height-1);
    fx_reset_clip();
    if (s_ctx_open) ctx_draw_abs();
    draw_debug_overlay();
}
static void redraw_region(int x1,int y1,int x2,int y2)
{ fx_set_color(s_bg); fx_fill_rect(x1,y1,x2,y2); draw_widget(&s_root,x1,y1,x2,y2); }
static void draw_canvas_only(fx_widget_t *w)
{
    /* v2.4.4: 与 draw_widget_inner 一致地做几何门槛 —— 此前这条路径只查可见标志, 退化矩形
     * (x2<x1 之类)在正常绘制时被跳过, 在这里却畅通, 这是"空下拉崩溃"能藏住的原因之一。 */
    if (w->x1 > w->x2 || w->y1 > w->y2) return;
    if (!(w->flags & FX_F_VISIBLE)) return;

    /* v2.4.3 动画: 除 canvas 外, 任何带 FX_F_ANIM 的可见控件也把自身矩形标脏, 让核心本帧重绘它。
     * 这是"全局动画开关"能作用于普通控件(如标签页药丸淡入、进度条缓动)的关键——
     * 之前 FX_F_ANIM 只对 canvas 生效, 所以普通控件设了标志也不会被继续重绘(动画只走一帧就停)。
     * 仅在动画开启时扫描, 关闭时零开销(不影响基准与金图)。 */
    if (s_anim_on) {
        int lim = s_pool_high > 0 ? s_pool_high : 0;
        for (int i = 0; i < lim; i++) {
            fx_widget_t *a = &s_pool[i];
            if (a->type == FX_W_NONE || !(a->flags & FX_F_ANIM)) continue;
            if (a->type == FX_W_CANVAS) continue;              /* 画布已在上面的分支里处理 */
            if (!(a->flags & FX_F_VISIBLE)) continue;
            if (!widget_in_active_page(a)) continue;
            fx_repaint_rect(a->x1, a->y1, a->x2, a->y2);
        }
    }
    if (w->type==FX_W_TAB) {
        fx_set_clip(w->x1,w->y1,w->x2,w->y2);
        float p2 = fx_page_wipe(w);
        if (p2 < 1.0f) { int b1,b2,b3,b4; fx_page_wipe_clip(w, w->x1,w->y1,w->x2,w->y2, p2, &b1,&b2,&b3,&b4); fx_set_clip(b1,b2,b3,b4); }
        for(fx_widget_t *c=w->child;c;c=c->sibling) if (c->page==w->value) draw_canvas_only(c);
        fx_reset_clip(); return;
    }   /* 子内容裁剪到 tab(+ 换页过渡时收窄) */
#if FXTK_WIDGET_CANVAS
    if (w->type==FX_W_CANVAS && w->cb && (w->flags & FX_F_ANIM && widget_in_active_page(w))) {
        if (w->flags & FX_F_BUF) { fxtk_off_begin(w); fxtk_draw_canvas(w); fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); fxtk_off_end(w); }
        else { fxtk_draw_canvas(w); fx_canvas_begin(w); w->cb(w,w->ud); fx_canvas_end(); }
    }
    if (w->type==FX_W_CANVAS)   /* C2: 内嵌子件补画到画布内容之上, 裁剪到画布 */
        for (fx_widget_t *c=w->child;c;c=c->sibling) draw_widget(c, w->x1,w->y1,w->x2,w->y2);
#endif
    for (fx_widget_t *c=w->child;c;c=c->sibling) draw_canvas_only(c);
}
void fxtk_draw_canvases(void) { if (!s_drv) return; draw_canvas_only(&s_root);
if (s_ctx_open) { fx_reset_clip(); ctx_draw_abs(); } draw_debug_overlay(); fxtk_draw_flush_all(); }

/* ================= 系统 API ================= */
void fx_init(const fx_driver_t *drv)
{

    /* v2.4.4 修复(评审 B8): 文档(docs/api.md, docs/guide.md 及英文版)一直教用户设 FXTK_AA=0|1|2,
     * 但此前全仓库只有 demo-main/app.c 读它 —— 用户自己的应用照做无效。这里让框架自己读,
     * 于是该开关对任何应用都成立(显式调用 fx_set_widget_aa 仍然优先, 因为在那之后执行)。 */
    { const char *aa = getenv("FXTK_AA"); if (aa && aa[0]) fx_set_widget_aa(atoi(aa)); }    s_drv=drv; fxtk_draw_set_driver(drv);
    memset(&s_root,0,sizeof(s_root)); s_root.type=FX_W_PANEL; s_root.flags=FX_F_VISIBLE; s_root.bg=s_bg;
    memset(s_pool,0,sizeof(s_pool));
    fxtk_anim_reset();   /* v2.4.4: 四张动画槽表也清零, 避免地址复用后继承死控件状态 */
    memset(s_wipe, 0, sizeof s_wipe);
    s_pressed=NULL; s_touch_prev=0; s_repaint=1; s_autorepaint=0;
    /* v2.3.1: 重初始化时清干净全部悬垂状态 (旧代码残留 ctx 弹层/滚轮目标/滚动池,
     * 重新 fx_init 后弹层指向已清零槽位而失效) */
    s_full=0; s_dirty_n=0; s_last_tx=-1; s_last_ty=-1;
    s_focus=NULL; s_scroll_drag=NULL; s_wheel_tgt=NULL; s_wheel_acc=0;
    s_ctxpop=NULL; s_ctx_te=NULL; s_ctx_open=0; s_ctx_hl=-1; s_sel_mode=0;
    memset(s_scroll_pool,0,sizeof(s_scroll_pool));
    fxtk_extra_reset();   /* v2.3.1: 清 list/drop 模块静态池与 s_pop 缓存指针 */
    fxtk_pool_reset_count();   /* v2.3: 池已 memset, 存活计数与分配游标同步归零 */
    fx_layout(); fx_log(FX_LOG_INFO, "fxtk ready: %dx%d, 控件 %d", drv->width, drv->height, fxtk_widget_count());
}
void fx_poll(void)
{
    if (!s_drv) return;
    if (s_drv->wheel_read) {
        int wx,wy,dy;
        while (s_drv->wheel_read(&wx,&wy,&dy)) {
            fx_widget_t *t = hit_test(&s_root,wx,wy);
            while (t && t->type != FX_W_SCROLL && t->type != FX_W_TEXTEDIT && t->type != FX_W_CANVAS) t = t->parent;
            if (t) {
                if (t->type == FX_W_CANVAS) {
                    if (s_wheel_tgt != t) { s_wheel_tgt = t; s_wheel_acc = 0; }
                    s_wheel_acc += dy;
                    /* anim 画布本来就会在本帧绘制; 不切换到全量路径, 避免滚动时闪一下 */
                    if (!(t->flags & FX_F_ANIM))
                        fx_repaint_rect(t->x1, t->y1, t->x2, t->y2);
                    continue;
                }
                int vis=(t->type==FX_W_TEXTEDIT)?(t->y2-t->y1-10):(t->y2-t->y1);
                int maxs=t->content_h-vis;
                if (maxs<0) maxs=0;
                t->scroll_y-=(int16_t)dy;
                if (t->scroll_y<0)t->scroll_y=0;
                if (t->scroll_y>maxs)t->scroll_y=(int16_t)maxs;
                fx_repaint_rect(t->x1,t->y1,t->x2,t->y2);
            }
        }
    }
    if (s_drv->touch_read) {
        int x=0,y=0,p=0;
        if (s_drv->touch_read(&x,&y,&p)) {
            s_last_tx=x; s_last_ty=y;   /* 悬停(未按)也更新坐标 */
            if (p && !s_touch_prev) fx_touch_press(x,y);
            /* v2.4.2 修复(用户反馈"悬停效果没了"): 原来只在指针【按住】时才调 fx_touch_move,
             * 而上下文菜单的悬停高亮就写在这个函数里 —— 于是"只把鼠标移上去"永远不会高亮。
             * 单纯移动也应当走这条路(拖拽类逻辑各自有 pressed 门闩, 不会误触发)。 */
            /* v2.4.2: 分支顺序很关键 —— 释放必须排在"移动"之前判断,
             * 否则释放事件会被移动分支吞掉, fx_touch_release 永远不执行, 点击就永远完不成
             * (曾因此导致"标签页无法切换"的回归, 用户实测反馈)。 */
            else if (!p && s_touch_prev) fx_touch_release(x,y);
            else if (s_touch_prev) fx_touch_move(x,y);
            else if (s_ctx_open && s_ctxpop) {
                /* 未按下时的移动: 只做上下文菜单的悬停高亮(用户反馈"悬停效果没了")。
                 * 单独放在这里而不是走 fx_touch_move, 避免干扰点击判定与各拖拽状态机。 */
                int cx1,cy1,cx2,cy2; fx_widget_rect(s_ctxpop,&cx1,&cy1,&cx2,&cy2);
                int hl=-1;
                if (x>=cx1&&x<=cx2&&y>=cy1&&y<=cy2) {
                    hl=(y-cy1)/((cy2-cy1)/5);
                    if (hl<0) hl=0;
                    if (hl>4) hl=4;
                }
                if (hl!=s_ctx_hl) { s_ctx_hl=hl; fx_repaint(); }
            }
            s_touch_prev=p;
            /* 调试文本固定宽度, 仅内容变化才更新: 避免每帧生成新纹理挤爆缓存 */
            if (s_tdbg_on) {
                static char last[48] = "";
                char nb[48];
                snprintf(nb,sizeof(nb),"T:%3d,%3d %s",x,y,p?"DOWN":"up  ");
                if (strcmp(nb,last)!=0) { strcpy(s_tdbg_str,nb); strcpy(last,nb); }
            }
        }
    }
    if(!s_ctxpop){ s_ctxpop=fx_canvas_new(pixel("-2000,-2000","-1900,-1900"),color(FX_WHITE));
    if (s_ctxpop){ fx_set_cb(s_ctxpop,ctx_draw,0); s_ctxpop->pos_mode=FX_POS_FIXED; s_ctxpop->page=-1; fx_set_visible(s_ctxpop,0); } }
    { int rx,ry;
    if (fxtk_right_click(&rx,&ry)){ fx_widget_t *hw=hit_test(&s_root,rx,ry);
    if (hw&&hw->type==FX_W_TEXTEDIT){ s_ctx_te=hw; s_ctx_open=1; s_ctx_hl=-1; int mw=64,mh=5*24;
    if (rx+mw>fx_width())rx=fx_width()-mw;
    if (ry+mh>fx_height())ry=fx_height()-mh;
    if (rx<0)rx=0;
    if (ry<0)ry=0; fx_widget_set_rect(s_ctxpop,rx,ry,rx+mw,ry+mh); fx_set_visible(s_ctxpop,0); } else { fx_set_visible(s_ctxpop,0); s_ctx_open=0; } fx_repaint(); } }
    if (s_drv->key_read) {
        fx_keyev_t ev;
        while (s_drv->key_read(&ev)) {
            s_last_key=ev;
            if (s_focus && s_focus->type==FX_W_TEXTEDIT && ev.down && widget_in_active_page(s_focus)) {
                int ro=(s_focus->flags&FX_F_READONLY);
                if (ev.mod && ev.utf8[0]) {
                    char c=ev.utf8[0]|32; int a,b;
                    if (c=='a') { s_focus->anchor=0; s_focus->caret=te_len(s_focus); }
                    else if (c=='l' && !ro) { if(s_focus->text_buf) s_focus->text_buf[0]=0; s_focus->caret=0; s_focus->anchor=0; }   /* Ctrl+L 清空 (v2.3.1: 只读时禁止) */
                    else if (c=='c'||c=='x') {
                        if (te_sel(s_focus,&a,&b) && s_drv->clip_set) { static char cb[4096]; int n=b-a;
                        if (n>4095)n=4095; memcpy(cb,s_focus->text_buf+a,(size_t)n); cb[n]=0; s_drv->clip_set(cb); }
                        if (c=='x' && !ro && te_sel(s_focus,&a,&b)) te_del_range2(s_focus,a,b);   /* v2.3.1: 只读时禁止剪切删除 */
                    }
                    else if (c=='v' && s_drv->clip_get) te_insert(s_focus,s_drv->clip_get());
                } else if (ev.utf8[0]&&!ro) te_insert(s_focus,ev.utf8);
                else if (ev.key==FX_KEY_RETURN&&!ro) te_insert(s_focus,"\n");   /* 换行 */
                else if (ev.key==FX_KEY_BACKSPACE&&!ro) { int a,b;
                if (te_sel(s_focus,&a,&b)) te_del_range2(s_focus,a,b); else te_backspace(s_focus); }
                else if (ev.key==FX_KEY_LEFT||ev.key==FX_KEY_RIGHT) { te_move(s_focus,ev.key);
                if (!(ev.mod&2)) s_focus->anchor=s_focus->caret; }
                else if (ev.key==FX_KEY_DELETE&&!ro) { int a,b;
                if (te_sel(s_focus,&a,&b))te_del_range2(s_focus,a,b); else te_del_range2(s_focus,s_focus->caret,te_next_off(s_focus->text_buf,s_focus->caret)); }
                else if (ev.key==FX_KEY_UP||ev.key==FX_KEY_DOWN) { te_move_vert(s_focus, ev.key==FX_KEY_UP?-1:1);
                if (!(ev.mod&2)) s_focus->anchor=s_focus->caret; te_caret_scroll(s_focus); }
                else if (ev.key==FX_KEY_HOME) { s_focus->caret=0; s_focus->anchor=0; }
                else if (ev.key==FX_KEY_END) { s_focus->caret=te_len(s_focus); s_focus->anchor=s_focus->caret; }
                te_caret_scroll(s_focus);
                fx_repaint_rect(s_focus->x1,s_focus->y1,s_focus->x2,s_focus->y2);
            }
        }
    }
    s_tick++;
    int bl=(s_tick/25)&1;
    if (s_focus && s_focus->type==FX_W_TEXTEDIT && bl!=s_blink) { s_blink=bl; fx_repaint_rect(s_focus->x1,s_focus->y1,s_focus->x2,s_focus->y2); }
    if (s_boot > 0) { s_boot--; fx_frame_begin(); fxtk_draw_all(); fx_frame_end(); s_repaint = 0; s_dirty_n = 0; s_full = 0; }
    else if (s_full) { s_full = 0; s_repaint = 0; s_dirty_n = 0;
fx_frame_begin(); fxtk_draw_all(); fx_frame_end(); }
else if (s_repaint && s_dirty_n > 0) {
        s_repaint=0; fx_frame_begin();
        for (int i=0;i<s_dirty_n;i++) redraw_region(s_dirty[i][0],s_dirty[i][1],s_dirty[i][2],s_dirty[i][3]);
        s_dirty_n=0;
        if (s_ctx_open) ctx_draw_abs(); fx_frame_end();
    } else if (s_repaint) { s_repaint=0; fx_frame_begin(); fxtk_draw_all(); fx_frame_end(); }
    else if (s_autorepaint) { fx_frame_begin(); fxtk_draw_all(); fx_frame_end(); }
    else fxtk_draw_canvases();
}
uint16_t fx_width(void){return s_drv?s_drv->width:0;} uint16_t fx_height(void){return s_drv?s_drv->height:0;}
void fx_set_autorepaint(int on){s_autorepaint=on;} void fx_set_touch_debug(int on){s_tdbg_on=on;}
void fx_repaint(void){s_full=1;s_repaint=1;s_dirty_n=0;}
/* ⚠️ v2.4.4 审计结论(第三方评审发现, 已核实): 本函数开头的 s_full=1 使下面的
 * 矩形合并与"局部重绘"分支变成【死代码】—— 消费端 fxtk_frame() 里 `else if (s_full)`
 * 排在脏区分支之前, 而本函数是全项目唯一往 s_dirty[] 写数据的地方, 于是永远走不到合并结果。
 * 现状: 任何一次请求都退化为【整帧重绘】(所以不会残影, 但也没有任何脏区优化)。
 * 这也是为什么当初加它是为了"杜绝残影": 局部路径历史上出过残影问题。
 * 要真正启用脏区合并, 需要: ①去掉这里的 s_full=1 ②把合并结果交给 redraw_region 逐块重绘
 * ③用连续多帧逐像素对比验证无残影(尤其滚动/悬停/光标闪烁/画布内控件移动)。 */
void fx_repaint_rect(int x1,int y1,int x2,int y2)
{ s_full=1;   /* 见上方说明: 这一行让下面的合并逻辑当前不可达 */
    if (!s_drv) return;
    if (x1<0)x1=0;
    if (y1<0)y1=0;
    if (x2>=s_drv->width)x2=s_drv->width-1;
    if (y2>=s_drv->height)y2=s_drv->height-1;
    if (x1>x2||y1>y2) return;
    int merged=1;
    while (merged) { merged=0;
        for (int i=0;i<s_dirty_n;i++) if (x1<=s_dirty[i][2]&&s_dirty[i][0]<=x2&&y1<=s_dirty[i][3]&&s_dirty[i][1]<=y2) {
            if (x1<s_dirty[i][0])s_dirty[i][0]=x1;
            if (y1<s_dirty[i][1])s_dirty[i][1]=y1;
            if (x2>s_dirty[i][2])s_dirty[i][2]=x2;
            if (y2>s_dirty[i][3])s_dirty[i][3]=y2;
            x1=s_dirty[i][0];y1=s_dirty[i][1];x2=s_dirty[i][2];y2=s_dirty[i][3];
            s_dirty[i][0]=s_dirty[s_dirty_n-1][0];s_dirty[i][1]=s_dirty[s_dirty_n-1][1];
            s_dirty[i][2]=s_dirty[s_dirty_n-1][2];s_dirty[i][3]=s_dirty[s_dirty_n-1][3];
            s_dirty_n--; i--; merged=1;
        }
    }
    if (s_dirty_n<FX_DIRTY_MAX) { s_dirty[s_dirty_n][0]=x1;s_dirty[s_dirty_n][1]=y1;s_dirty[s_dirty_n][2]=x2;s_dirty[s_dirty_n][3]=y2;s_dirty_n++;s_repaint=1; }
    else fx_repaint();
}
void fx_set_bg(fx_color_t c){s_bg=c;s_root.bg=c;fx_repaint();}

/* 主题化全局背景: 设置浅/深两套背景, 当前主题决定实际使用哪一套 */
void fx_set_global_background(fx_colorx_t c)
{
    s_global_bg = c;
    s_bg = fx_colorx_current(c);
    s_root.bg = s_bg;
    fx_repaint();
}

void fx_set_dark_theme(int dark)
{
    s_dark_theme = dark ? 1 : 0;
    s_bg = fx_colorx_current(s_global_bg);
    s_root.bg = s_bg;

    /* 统一重刷主题相关控件: 文本/复选框透明, 标签页/面板/网格跟随深色 */
    for (int i = 0; i < FX_MAX_WIDGETS; i++) {
        fx_widget_t *w = &s_pool[i];
        if (w->type == FX_W_NONE) continue;
        switch (w->type) {
        case FX_W_LABEL:
        case FX_W_CHECKBOX:
            w->bg = FX_BLACK;
            w->fg = s_dark_theme ? FX_RGB(235, 235, 235) : FX_RGB(40, 40, 40);
            break;
        case FX_W_CANVAS:
            w->bg = FX_RGB(30, 30, 34);
            break;
        case FX_W_TAB:
        case FX_W_GRID:
        case FX_W_PANEL:
            w->bg = s_dark_theme ? FX_RGB(45, 45, 50) : FX_RGB(224, 224, 224);
            break;
        default:
            break;
        }
    }
    fx_repaint();
}

int fx_is_dark_theme(void) { return s_dark_theme; }
fx_color_t fx_colorx_current(fx_colorx_t c) { return s_dark_theme ? c.dark : c.light; }
/* v2.4.5 修复: fx_set_title 此前【只写 w->title】, 而文本框绘制读的是
 * fxtk_draw_textedit() 里的 `w->text_buf ? w->text_buf : w->title`。
 * 创建文本框时 text_buf 必定被分配(见 fx_widget_new_impl), 于是 text_buf 永远优先 ——
 * 对一个文本框调 fx_set_title 是【完全无效】的: 既不改显示, 也不改 fx_textedit_text(),
 * 表现为"点按钮了但输入框内容不变"。同时公共 API 里只有 fx_textedit_text() 这个取值函数,
 * 没有任何设值函数 ⇒ 应用无法用官方途径改写文本框内容(计算器/时钟此类程序直接卡死)。
 * 现在对文本框把内容写进 text_buf(并同步 title 以便 fx_widget_title 仍返回真实内容),
 * 非文本框保持原行为不变。 */
void fx_set_title(fx_widget_t *w,const char *s)
{
    if(!w)return;
    s = s ? s : "";
    strncpy(w->title, s, sizeof(w->title)-1); w->title[sizeof(w->title)-1]=0;
    if (w->type == FX_W_TEXTEDIT && w->text_buf) {
        int ul = (int)strlen(s);
        if (w->text_max > 0) {                     /* 与 te_insert 一致: 尊重字数上限(按字符, 不切断 UTF-8) */
            if (te_chars_n(s, ul) > w->text_max) {
                int i = 0, c = 0;
                while (i < ul && c < w->text_max) { if (((unsigned char)s[i] & 0xC0) != 0x80) c++; i++; }
                ul = i;
            }
        }
        if (te_grow(w, ul + 1)) {                  /* OOM 时放弃本次改写, 不越界 */
            memcpy(w->text_buf, s, (size_t)ul);
            w->text_buf[ul] = 0;
        }
        w->caret = w->anchor = (int)strlen(w->text_buf);
        te_caret_scroll(w);                        /* 内容变长时把光标滚进可视区 */
    }
    redraw_widget_now(w);
}
void fx_set_color_w(fx_widget_t *w,fx_color_t c){ if(!w)return; w->bg=c; redraw_widget_now(w); }
void fx_set_value(fx_widget_t *w,int v){ if(!w)return;
if (v<0)v=0;
if (v>100)v=100;
if (w->value!=(int16_t)v){ w->value=(int16_t)v; redraw_widget_now(w); } }
int fx_get_value(const fx_widget_t *w){return w?w->value:0;}
void fx_set_cb(fx_widget_t *w,fx_cb_t cb,void *ud){ if(!w)return; w->cb=cb; w->ud=ud; }
void fx_set_visible(fx_widget_t *w,int vis){ if(!w)return;
if (vis)w->flags|=FX_F_VISIBLE; else w->flags&=(uint16_t)~FX_F_VISIBLE; fx_layout(); fx_repaint(); }
int fx_widget_type(const fx_widget_t *w){return w?w->type:FX_W_NONE;}
const char *fx_widget_title(const fx_widget_t *w){return w?w->title:NULL;}
const char *fx_textedit_text(fx_widget_t *w){ if(!w)return NULL; return w->text_buf ? w->text_buf : w->title; }
/* v2.4.5: 补上声明已久的实现(此前只有 fxtk.h 声明 + docs 记载, 无定义 —— 调用者必然 undefined reference)。
 * 只读由 fx_frame()/ctx_do() 里的 `flags & FX_F_READONLY` 判定, 覆盖输入/退格/删除/剪切/粘贴/清空。
 * 注: flags 是 uint16_t, 故掩码用 (uint16_t) 而非周边旧代码的 (uint8_t) —— 后者会保留 bit9 无法清除。 */
void fx_textedit_set_readonly(fx_widget_t *w,int ro){
    if (!w || w->type != FX_W_TEXTEDIT) return;
    uint16_t next = ro ? (uint16_t)(w->flags | FX_F_READONLY)
                       : (uint16_t)(w->flags & (uint16_t)~FX_F_READONLY);
    if (next == w->flags) return;
    w->flags = next;
    redraw_widget_now(w);
}
void fx_set_fgcolor_w(fx_widget_t *w, fx_color_t c){ if(!w)return; w->fg=c; redraw_widget_now(w); }
void fx_widget_rect(const fx_widget_t *w,int *x1,int *y1,int *x2,int *y2){ if(!w)return;
if (x1)*x1=w->x1;
if (y1)*y1=w->y1;
if (x2)*x2=w->x2;
if (y2)*y2=w->y2; }
fx_color_t fx_get_bg(void){return s_bg;}

/* ================= TEXTEDIT 内核 ================= */
static int te_next(const char *s,int i){ unsigned char c=(unsigned char)s[i]; int l=1;
if (c>=0xF0)l=4; else if(c>=0xE0)l=3; else if(c>=0xC0)l=2; i+=l; 
while(((unsigned char)s[i]&0xC0)==0x80)i++; return i; }
static void te_caret_scroll(fx_widget_t *w){
    const char *txt=w->text_buf?w->text_buf:w->title;
    int len=(int)strlen(txt);
    int aw=(w->x2-w->x1+1)-12, lh=22;
    /* v2.4.5 加固: 原循环在 acc+cw>aw 成立而 j>start 不成立(即"当前字符就超宽, 但
     * 这一行还没放下任何字符")时会走 continue, 此时 i2 不动、line 不动 ⇒ 死循环。
     * 只要 aw<=0 就必然触发: 例如驱动未初始化(fx_layout 见 s_drv 为空直接 return,
     * 控件坐标停在 0 → aw = -11), 或控件被挤到退化尺寸。
     * 现在: ①aw<=0 直接放弃滚动计算; ②每次迭代强制推进 i2, 保证循环一定终止。 */
    if (aw <= 0 || len <= 0) return;
    int line=0, start=0, acc=0, i2=0;
    while(i2<len){
        if(txt[i2]=='\n'){ if(w->caret<=i2)break; line++; i2++; start=i2; acc=0; continue; }
        int j=te_next(txt,i2);
        if (j <= i2) j = i2 + 1;                 /* 强制前进, 杜绝零推进 */
        if (j > len) j = len;
        int cw=fx_text_width_n(txt+i2,j-i2);
        if (acc+cw>aw && j>start){ if(w->caret<=i2)break; line++; start=i2; acc=0; continue; }
        acc+=cw; i2=j;
        if (i2>=w->caret)break;
    }
    int caret_y=line*lh, vis_h=w->y2-w->y1-10;
    if (caret_y < w->scroll_y) w->scroll_y=(int16_t)caret_y;
    if (caret_y+lh > w->scroll_y+vis_h) w->scroll_y=(int16_t)(caret_y+lh-vis_h);
    if (w->scroll_y<0)w->scroll_y=0;
}

static fx_widget_t *s_ctxpop=NULL,*s_ctx_te=NULL;
static void ctx_do(int idx){ fx_widget_t *w=s_ctx_te;
if (!w)return; int a,b; int ro=(w->flags&FX_F_READONLY);
if (idx==0){ if(te_sel(w,&a,&b)&&s_drv->clip_set){ static char cb[4096]; int n=b-a;
if (n>4095)n=4095; memcpy(cb,w->text_buf+a,(size_t)n); cb[n]=0; s_drv->clip_set(cb); } }
  else if(idx==1){ if(!ro&&te_sel(w,&a,&b)){ if(s_drv->clip_set){ static char cb[4096]; int n=b-a;
  if (n>4095)n=4095; memcpy(cb,w->text_buf+a,(size_t)n); cb[n]=0; s_drv->clip_set(cb);} te_del_range2(w,a,b); } }
  else if(idx==2){ if(!ro&&s_drv->clip_get) te_insert(w,s_drv->clip_get()); }
  else if(idx==3){ w->anchor=0; w->caret=te_len(w); }
  else if(idx==4){ if(!ro&&w->text_buf){ w->text_buf[0]=0; w->caret=0; w->anchor=0; } }
  fx_set_visible(s_ctxpop,0); s_ctx_open=0; fx_repaint();
}
static const char *s_ctx_items[5]={"复制","剪切","粘贴","全选","清空"};
static void ctx_draw(fx_widget_t*w,void*ud){
  int x1=0,y1=0,x2=0,y2=0; fx_widget_rect(w,&x1,&y1,&x2,&y2); int cw=x2-x1,ch=y2-y1,rh=ch/5;
  fx_set_color(FX_WHITE); fx_fill_rect(0,0,cw-1,ch-1);
  fx_set_color(FX_GRAY); fx_draw_rect(0,0,cw-1,ch-1);
  for(int i=0;i<5;i++){ if(i==s_ctx_hl){fx_set_color(FX_RGB(33,150,243));fx_fill_rect(1,1+i*rh,cw-2,1+i*rh+rh-1);}
    fxtk_draw_text_size(14,6,3+i*rh,s_ctx_items[i], i==s_ctx_hl?FX_WHITE:FX_RGB(40,40,40), i==s_ctx_hl?FX_RGB(33,150,243):FX_WHITE); }
}
#define TE_ISW(b) (((unsigned char)(b))>=0x80 || ((b)>='a'&&(b)<='z')||((b)>='A'&&(b)<='Z')||((b)>='0'&&(b)<='9')||(b)=='_')
static int te_word_start(const char*s,int i){ while(i>0&&!TE_ISW(s[i-1]))i--; 
while(i>0&&TE_ISW(s[i-1]))i--; return i; }
static int te_word_end(const char*s,int i){ while(s[i]&&!TE_ISW(s[i]))i++; 
while(s[i]&&TE_ISW(s[i]))i++; return i; }
static int te_para_start(const char*s,int i){ while(i>0&&s[i-1]!='\n')i--; return i; }
static int te_para_end(const char*s,int i){ while(s[i]&&s[i]!='\n')i++; return i; }
static void te_sel_para(fx_widget_t*w){ const char*s=w->text_buf?w->text_buf:""; int a=te_para_start(s,w->caret); w->anchor=a; w->caret=te_para_end(s,w->caret); }
static void te_sel_word(fx_widget_t *w){ const char *s=w->text_buf?w->text_buf:""; int i=w->caret;
  #define ISW(b) (((unsigned char)(b))>=0x80 || ((b)>='a'&&(b)<='z')||((b)>='A'&&(b)<='Z')||((b)>='0'&&(b)<='9')||(b)=='_')
  while(i>0&&ISW(s[i-1]))i--; 
  while(i>0&&(((unsigned char)s[i-1])>=0x80))i--;
  int a=i; i=w->caret; 
  while(s[i]&&ISW(s[i]))i=te_next_off(s,i);
  w->anchor=a; w->caret=i; }

static int te_len(fx_widget_t *w){return (int)strlen(w->text_buf?w->text_buf:"");}
static int te_chars_n(const char *s,int n){int c=0,i=0;while(i<n&&s[i]){if(((unsigned char)s[i]&0xC0)!=0x80)c++;i++;}return c;}
static int te_chars(fx_widget_t *w){return te_chars_n(w->text_buf?w->text_buf:"",te_len(w));}
static int te_grow(fx_widget_t *w,int need){ if(need<w->text_cap)return 1; int nc=need*2; char *nb=(char*)realloc(w->text_buf,(size_t)nc);
if (nb){w->text_buf=nb;w->text_cap=nc;return 1;} return 0; }   /* v2.3.1: 返回成败, OOM 调用方必须放弃插入 */
static int te_prev_off(const char *s,int off){ if(!s)return 0; int i=off-1;while(i>0&&(((unsigned char)s[i]&0xC0)==0x80))i--;return i<0?0:i;}
static int te_next_off(const char *s,int off){ if(!s)return 0; int len=(int)strlen(s),i=off+1;while(i<len&&(((unsigned char)s[i]&0xC0)==0x80))i++;return i>len?len:i;}
static int te_sel(fx_widget_t *w,int *a,int *b){int x=w->caret,y=w->anchor;
if (x>y){int t=x;x=y;y=t;}*a=x;*b=y;return y>x;}
static void te_del_range2(fx_widget_t *w,int a,int b){char *s=w->text_buf;
if (!s||a>=b)return;int len=(int)strlen(s);memmove(s+a,s+b,(size_t)(len-b)+1);w->caret=a;w->anchor=a;}
static void te_insert(fx_widget_t *w,const char *utf8)
{
    char *s=w->text_buf;
    if (!s||!utf8)return;
    int a,b;
    if (te_sel(w,&a,&b)) te_del_range2(w,a,b);
    int ul=(int)strlen(utf8);
    if (w->text_max>0) { int free_n=w->text_max-te_chars(w);
    if (free_n<=0)return;
    if (te_chars_n(utf8,ul)>free_n) { int i=0,c=0; 
    while(i<ul&&c<free_n){if(((unsigned char)utf8[i]&0xC0)!=0x80)c++;i++;} ul=i;
    if (ul<=0)return; } }
    int len=(int)strlen(s);
    if (!te_grow(w,len+ul+1)) return;   /* v2.3.1: OOM 放弃本次插入, 防越界写 */
    s=w->text_buf;
    memmove(s+w->caret+ul,s+w->caret,(size_t)(len-w->caret)+1);
    memcpy(s+w->caret,utf8,(size_t)ul);
    w->caret+=ul; w->anchor=w->caret;
}
static void te_backspace(fx_widget_t *w){ if(w->caret<=0)return; int p=te_prev_off(w->text_buf,w->caret); te_del_range2(w,p,w->caret); }
static void te_move(fx_widget_t *w,int key){ if(key==FX_KEY_LEFT)w->caret=te_prev_off(w->text_buf,w->caret); else w->caret=te_next_off(w->text_buf,w->caret); }
static void te_move_vert(fx_widget_t *w,int dir){
    const char *s=w->text_buf?w->text_buf:""; int len=(int)strlen(s);
    static int st[512]; static int se[512];
    int aw=(w->x2-w->x1+1)-12;
    int nl=1;st[0]=0;se[0]=len;int acc=0,i2=0;
    while(i2<len&&nl<511){ if(s[i2]=='\n'){se[nl-1]=i2;st[nl]=i2+1;se[nl]=len;nl++;acc=0;i2++;continue;} int j=te_next_off(s,i2);int cw=fx_text_width_n(s+i2,j-i2);
    if (acc+cw>aw&&j>st[nl-1]){se[nl-1]=i2;st[nl]=i2;se[nl]=len;nl++;acc=0;continue;} acc+=cw;i2=j; }
    se[nl-1]=len;
    int Lc=0; 
    for(int L=0;L<nl;L++) if(w->caret>=st[L])Lc=L;
    int col=0; { int i=st[Lc]; 
    while(i<w->caret){i=te_next_off(s,i);col++;} }
    int Lt=Lc+dir;
    if (Lt<0)Lt=0;
    if (Lt>=nl)Lt=nl-1;
    int i=st[Lt]; int cc=0; 
    while(i<se[Lt]&&cc<col){i=te_next_off(s,i);cc++;}
    w->caret=i;
}
static void te_caret_from_xy(fx_widget_t *w,int x,int y)
{
    const char *s=w->text_buf?w->text_buf:""; int len=(int)strlen(s);
    static int st[512]; static int se[512];
    int aw=(w->x2-w->x1+1)-12, lh=22;
    int nl=1; st[0]=0; se[0]=len; int acc=0,i2=0;
    while(i2<len&&nl<511){
        if(s[i2]=='\n'){se[nl-1]=i2;st[nl]=i2+1;se[nl]=len;nl++;acc=0;i2++;continue;}
        int j=te_next_off(s,i2); int cw=fx_text_width_n(s+i2,j-i2);
        if (acc+cw>aw&&j>st[nl-1]){se[nl-1]=i2;st[nl]=i2;se[nl]=len;nl++;acc=0;continue;}
        acc+=cw;i2=j;
    }
    se[nl-1]=len;
    int ty0=w->y1+5, L=(y-ty0+w->scroll_y)/lh;
    if (L<0)L=0;
    if (L>=nl)L=nl-1;
    int rel=x-(w->x1+6), i=st[L]; acc=0;
    while(i<se[L]){ int j=te_next_off(s,i); int cw=fx_text_width_n(s+i,j-i);
    if (rel<=acc+cw/2)break; acc+=cw;i=j; }
    w->caret=i;
}
static void scroll_drag_to(fx_widget_t *w,int y)
{
    int vis=(w->type==FX_W_TEXTEDIT)?(w->y2-w->y1-10):(w->y2-w->y1+1);
    int total=w->content_h;
    if (total<=vis) return;
    int track0=w->y1+2, track1=w->y2-2, th=vis*vis/total;
    if (th<20)th=20;
    int span=(track1-track0)-th;
    if (span<=0) return;
    int sc=(int)(((long)(y-track0-th/2)*(total-vis))/span);
    if (sc<0)sc=0;
    if (sc>total-vis)sc=total-vis;
    if (sc!=w->scroll_y) {
        w->scroll_y=(int16_t)sc;
        if (w->type==FX_W_CANVAS) { fx_scroll_state_t *st=scroll_state(w); if (st){ st->tgt=(float)sc; st->off=(float)sc; st->last=sc; } }   /* canvas 滑块拖动: 同步滚动状态池 (v2.3.1: 池满判空) */
        fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
    }
}
const char *fx_textedit_get(fx_widget_t *w){return (w&&w->text_buf)?w->text_buf:(w?w->title:"");}
void fx_textedit_clear(fx_widget_t *w){ if(!w)return;
if (w->text_buf)w->text_buf[0]=0; w->caret=0;w->anchor=0; fx_repaint_rect(w->x1,w->y1,w->x2,w->y2); }

/* ================= 桌面扩展 API ================= */
void fx_set_focus(fx_widget_t *w){ s_focus=w;
if (w) fx_repaint_rect(w->x1,w->y1,w->x2,w->y2); }
fx_widget_t *fx_get_focus(void){return s_focus;}
int fx_focus_blink(void){return s_blink;}
fx_keyev_t fx_last_key(void){return s_last_key;}
void fx_touch_state(int *x,int *y,int *pressed){ if(x)*x=s_last_tx;
if (y)*y=s_last_ty;
if (pressed)*pressed=s_touch_prev; }
fx_widget_t *fx_pressed(void){return s_pressed;}
void fx_set_image(fx_widget_t *w,fx_image_t *img){ if(!w)return; w->img=img; fx_repaint_rect(w->x1,w->y1,w->x2,w->y2); }
void fx_image_set_zoom(fx_widget_t *w,int p){ if(!w||w->type!=FX_W_IMAGE)return;
if (p<10)p=10;
if (p>400)p=400; w->value=(int16_t)p; fx_repaint_rect(w->x1,w->y1,w->x2,w->y2); }
void fx_widget_set_rect(fx_widget_t *w,int x1,int y1,int x2,int y2)
{
    if (!w) return;
    if (x1!=w->x1||y1!=w->y1||x2!=w->x2||y2!=w->y2) {
        fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
        w->x1=(int16_t)x1;w->y1=(int16_t)y1;w->x2=(int16_t)x2;w->y2=(int16_t)y2;
        fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
    }
}
/* 固定坐标模式: 布局重算不再按 ox1/oy1 复位, 并记录移动基准点 (动态控件用) */
void fx_widget_fix(fx_widget_t *w,int x1,int y1)
{
    if (!w) return;
    w->pos_mode = FX_POS_FIXED;
    w->ox1 = (int16_t)x1; w->oy1 = (int16_t)y1;
    fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
    w->x1=(int16_t)x1; w->y1=(int16_t)y1;
    fx_repaint_rect(w->x1,w->y1,w->x2,w->y2);
}
void fx_scroll_content(fx_widget_t *w,int h){ if (w) w->content_h=(int16_t)h; }
void fx_set_fgcolor(fx_widget_t *w,fx_color_t c){ if(!w)return; w->fg=c; fx_repaint_rect(w->x1,w->y1,w->x2,w->y2); }
int fx_wheel_take(fx_widget_t *w) { if (s_wheel_tgt != w) return 0; int d = s_wheel_acc; s_wheel_acc = 0; return d; }

void fx_set_window_title(const char *s) { if (s_drv && s_drv->set_title) s_drv->set_title(s); }

static int s_grid_lines = 0;
void fx_set_grid_lines(int on) { s_grid_lines = on; fx_repaint(); }
int fxtk_grid_lines_on(void) { return s_grid_lines; }

/* ---- 核心丝滑滚动: 目标像素 + 25% 逐帧插值 (rc 手感), 应用层一行调用 ---- */
/* 状态池: 多控件并行滚动互不干扰 (定义已前移至文件头部静态区, 供 unlink_free/fx_init 清理) */
static fx_scroll_state_t *scroll_state(fx_widget_t *w)
{
    for (int i = 0; i < 8; i++)
        if (s_scroll_pool[i].w == w) return &s_scroll_pool[i];
    for (int i = 0; i < 8; i++)
        if (!s_scroll_pool[i].w) { s_scroll_pool[i].w = w; return &s_scroll_pool[i]; }
    /* v2.3.1: 池满不再静默别名到 [0] (第 9 个控件会和第 1 个互踩), 由调用方退化处理 */
    fx_log(FX_LOG_WARN, "滚动状态池已满(%d 个并发): 加大 FX_MAX_SCROLL_STATES", FX_MAX_SCROLL_STATES);
    return NULL;
}
/* 更新滚动: 返回当前偏移(整数); 滚轮转多少内容滚多少(像素), 停后 25%/帧 收尾
 * 有变化才请求重绘(静止零重绘); 状态池支持多控件并行滚动 */
int fx_scroll_update(fx_widget_t *w, int content_h)
{
    if (!w) return 0;
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int ch=y2-y1+1; int maxs=content_h-ch;
    if (maxs<0)maxs=0;
    w->content_h = (int16_t)(content_h > 32000 ? 32000 : content_h);   /* 供滑块拖动/滚动条使用 */
    fx_scroll_state_t *s = scroll_state(w);
    if (!s) {   /* v2.3.1: 池满退化: 无缓动直滚, 行为仍正确 */
        int cur = w->scroll_y - (int16_t)fx_wheel_take(w);
        if (cur < 0) cur = 0;
        if (cur > maxs) cur = maxs;
        w->scroll_y = (int16_t)cur;
        return cur;
    }
    s->tgt -= (float)fx_wheel_take(w);          /* 轮上=内容上滚 */
    if (s->tgt < 0) s->tgt = 0;
    if (s->tgt > maxs) s->tgt = (float)maxs;
    s->off += (s->tgt - s->off) * 0.25f;        /* 25% 逐帧插值 */
    if (s->tgt - s->off > -1 && s->tgt - s->off < 1) s->off = s->tgt;   /* 收敛对齐 */
    int cur = (int)(s->off + 0.5f);
    if (cur != s->last) {
        s->last = cur;
        if (!(w->flags & FX_F_ANIM))            /* anim 画布核心自动重绘 */
            fx_repaint_rect(x1, y1, x2, y2);
    }
    w->scroll_y = (int16_t)cur;
    return cur;
}
/* 统一滚动 API: 直接设滚动目标(供滚动条拖拽等), 走同一套 scroll_state 缓动 */
void fx_set_scroll(fx_widget_t *w, int off){ if(!w)return; fx_scroll_state_t *s=scroll_state(w); 
if(s){ s->tgt=(float)off; } }
void fx_scrollbar_draw(fx_widget_t *w,int off,int content_h)
{
    if(!w)return;
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    if (content_h<=ch)return;
    int th=ch*ch/content_h;
    if (th<20)th=20;
    int ty=(int)((long)off*(ch-th)/(content_h-ch));
    fx_set_color(FX_GRAY); fx_fill_rect(cw-5,2,cw-2,ch-2);
    fx_set_color(FX_RGB(33,150,243)); fx_fill_rect(cw-5,2+ty,cw-2,2+ty+th);
}

/* 画布离屏缓冲开关: 大画布/直绘场景关闭以避免放大合成问题 */
void fx_canvas_set_buf(fx_widget_t *w, int on)
{
    if (!w || w->type != FX_W_CANVAS) return;
    if (on) { fx_canvas_enable_buf(w); }
    else {
        if (w->offbuf) { free(w->offbuf); w->offbuf = 0; w->offw = w->offh = 0; }
        w->flags &= (uint8_t)~FX_F_BUF;
    }
}
