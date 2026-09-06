/**
 * fxtk.h — FX Toolkit 公共头文件
 */
#ifndef FXTK_H
#define FXTK_H

/* 框架版本 (发布标记) */
#define FXTK_VERSION "2.3"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 颜色: 24bit RGB (0xRRGGBB), 与主流标准一致 */
typedef uint32_t fx_color_t;

/* 主题化颜色: 同时提供浅色/深色两套值, 由全局主题自动选择 */
typedef struct {
    fx_color_t light;
    fx_color_t dark;
} fx_colorx_t;

#define FX_RGB(r, g, b) \
    ((fx_color_t)((((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF)))
#define FX_RED    0xFF0000
#define FX_GREEN  0x00FF00
#define FX_BLUE   0x0000FF
#define FX_WHITE  0xFFFFFF
#define FX_BLACK  0x000000
#define FX_YELLOW 0xFFFF00
#define FX_CYAN   0x00FFFF
#define FX_MAGENTA 0xFF00FF
#define FX_GRAY   0x848484
#define FX_LGRAY  0xC8C8C8

/* ---- 主题化命名词 (与默认配色一致, 消除各处 240/245 不一致) ---- */
#define FX_WINDOW_BG   FX_RGB(245,245,245)  /* 浅色窗口背景 */
#define FX_WINDOW_DARK FX_RGB(30,30,34)     /* 深色窗口背景 */
#define FX_UI_FG       FX_RGB(40,40,40)     /* 默认深字 */
#define FX_BTN_BLUE    FX_RGB(33,150,243)   /* 蓝按钮 */
#define FX_OK_GREEN    FX_RGB(76,175,80)    /* 绿滑条/进度 */
#define FX_RED_ACCENT  FX_RGB(244,67,54)    /* 红色强调 */
#define FX_PURPLE      FX_RGB(156,39,176)   /* 紫色 */
#define FX_CANVAS_DARK FX_RGB(30,30,30)     /* 深色画布底 */

struct fx_widget;
typedef struct fx_widget fx_widget_t;
typedef void (*fx_cb_t)(fx_widget_t *w, void *ud);

typedef enum {
    FX_A_NONE = 0,
    FX_A_PIXEL,
    FX_A_PERCENT,
    FX_A_GRID,
    FX_A_TITLE,
    FX_A_NAME,
    FX_A_CALL,
    FX_A_LINE,
    FX_A_ROW,
    FX_A_COLOR,
    FX_A_FGCOLOR,
    FX_A_DENSE,
    FX_A_BORDER,
    FX_A_RADIUS,
    FX_A_VALUE,
    FX_A_WIDGET,
    FX_A_PAGE,
    FX_A_ANIM,
    FX_A_IMAGE,
    FX_A_MAXLEN,
    FX_A_SIDEBAR,
} fx_attr_tag_t;

typedef struct {
    fx_attr_tag_t tag;
    union {
        struct { int16_t x1, y1, x2, y2; } rect;
        struct { int16_t p1, p2, p3, p4; } pct;
        struct { const char *name; int16_t r1, c1, r2, c2; } grid;
        struct { const char *s; } str;
        struct { fx_cb_t cb; } cb;
        struct { int16_t v; } iv;
        struct { fx_color_t c; } color;
        struct { fx_widget_t *w; } w;
    } v;
} fx_attr_t;

#define FX_ATTR_END { FX_A_NONE, { {0} } }

fx_attr_t pixel(const char *a, const char *b);
fx_attr_t percent(const char *a, const char *b);
fx_attr_t grid1(const char *name);
fx_attr_t grid5(const char *name, int r1, int c1, int r2, int c2);
#define FX_GRID_SEL(_1,_2,_3,_4,_5,NAME,...) NAME
#define grid(...) FX_GRID_SEL(__VA_ARGS__, grid5, grid5, grid5, grid5, grid1)(__VA_ARGS__)
fx_attr_t title(const char *s);
fx_attr_t text(const char *s);
fx_attr_t name(const char *s);
fx_attr_t call(fx_cb_t cb);
fx_attr_t line(int n);
fx_attr_t row(int n);
fx_attr_t color(fx_color_t c);
fx_attr_t fgcolor(fx_color_t c);
fx_attr_t border(int n);
fx_attr_t radius(int n);
#define FX_POS_FIXED 3   /* 布局不重算(弹层直写坐标) */
#define FX_F_DENSE (1<<7)   /* grid 密集模式 */
#define FX_F_FIT   (1<<8)   /* 适应: 封顶+居中 */
#define FX_F_READONLY (1<<9)   /* 文本框只读 */
void fx_textedit_set_readonly(fx_widget_t *w,int ro);
const char *fx_textedit_text(fx_widget_t *w);   /* v2.3: 取文本框当前内容(可能为 NULL 或空串) */
void fx_set_fit(fx_widget_t *w, int on);
void fxtk_fit_rect(fx_widget_t *w,int*x1,int*y1,int*x2,int*y2);
int  fxtk_max_scale1000(void);
fx_attr_t dense(void);
fx_attr_t value(int n);
fx_attr_t page(int n);
/* 标签页侧边栏方位: 0=上, 1=左, 2=右, 3=下 (英文标签较长时常用左右侧; 默认上) */
#define FX_TAB_TOP 0
#define FX_TAB_LEFT 1
#define FX_TAB_RIGHT 2
#define FX_TAB_BOTTOM 3
fx_attr_t sidebar(int side);
fx_attr_t anim(int n);
fx_attr_t fx_wptr(fx_widget_t *w);

enum {
    FX_W_NONE = 0,
    FX_W_BUTTON,
    FX_W_LABEL,
    FX_W_GRID,
    FX_W_CANVAS,
    FX_W_SLIDER,
    FX_W_PROGRESS,
    FX_W_CHECKBOX,
    FX_W_PANEL,
    FX_W_TAB,
    FX_W_IMAGE = 16,
    FX_W_TEXTEDIT = 17,
    FX_W_SCROLL = 19,
    FX_W_COUNT
};

fx_widget_t *fx_widget_new_impl(int type, fx_attr_t attrs[]);

#define fx_button_new(...)   fx_widget_new_impl(FX_W_BUTTON,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_label_new(...)    fx_widget_new_impl(FX_W_LABEL,    (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_grid_map(...)     fx_widget_new_impl(FX_W_GRID,     (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_canvas_new(...)   fx_widget_new_impl(FX_W_CANVAS,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_slider_new(...)   fx_widget_new_impl(FX_W_SLIDER,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_progress_new(...) fx_widget_new_impl(FX_W_PROGRESS, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_checkbox_new(...) fx_widget_new_impl(FX_W_CHECKBOX, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_panel_new(...)    fx_widget_new_impl(FX_W_PANEL,    (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_tab_new(...)      fx_widget_new_impl(FX_W_TAB,      (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})

void fx_parent(fx_widget_t *p);
fx_widget_t *fx_find(const char *name);
void fx_delete_impl(fx_attr_t attrs[]);
#define fx_delete(...) fx_delete_impl((fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})

void fx_set_title(fx_widget_t *w, const char *s);
void fx_set_color_w(fx_widget_t *w, fx_color_t c);
void fx_set_fgcolor_w(fx_widget_t *w, fx_color_t c);   /* v2.3: 改控件前景/文字色 */
void fx_set_value(fx_widget_t *w, int v);
int  fx_get_value(const fx_widget_t *w);
void fx_set_cb(fx_widget_t *w, fx_cb_t cb, void *ud);
void fx_set_visible(fx_widget_t *w, int vis);
int  fx_widget_type(const fx_widget_t *w);
const char *fx_widget_title(const fx_widget_t *w);
void fx_widget_set_rect(fx_widget_t *w, int x1, int y1, int x2, int y2);
void fx_widget_rect(const fx_widget_t *w, int *x1, int *y1, int *x2, int *y2);

void fx_layout(void);
void fx_set_max_scale(float f);   /* 控件缩放上限(倍), 默认1.6 */
void fxtk_apply_fit(fx_widget_t *w);
void fx_touch_press(int x, int y);
void fx_touch_release(int x, int y);
void fx_touch_move(int x, int y);

void fx_frame_begin(void);
void fx_frame_end(void);
int  fx_band_index(void);
void fx_repaint(void);
void fx_repaint_rect(int x1, int y1, int x2, int y2);

void fx_set_color(fx_color_t c);
void fx_set_clip(int x1, int y1, int x2, int y2);
void fx_reset_clip(void);
void fx_draw_pixel(int x, int y);
void fx_draw_hline(int x1, int x2, int y);
void fx_draw_vline(int x, int y1, int y2);
void fx_draw_line(int x1, int y1, int x2, int y2);
void fx_draw_rect(int x1, int y1, int x2, int y2);
void fx_fill_rect(int x1, int y1, int x2, int y2);
void fx_fill_rect_gradient(int x1, int y1, int x2, int y2, fx_color_t c1, fx_color_t c2, int vertical);  /* v2.2 渐变填充 (vertical=1 上下, 0 左右) */
void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_fill_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_draw_circle(int cx, int cy, int r);
void fx_fill_circle(int cx, int cy, int r);
void fx_draw_ellipse(int cx, int cy, int rx, int ry);
void fx_fill_ellipse(int cx, int cy, int rx, int ry);
void fx_draw_arc(int cx, int cy, int r, int a1, int a2);
void fx_fill_arc(int cx, int cy, int r, int a1, int a2);
void fx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_draw_polygon(const int16_t *pts, int n);
void fx_set_aa(int on);   /* 抗锯齿开关 (默认关): 开时 line/circle/rect 等矢量图元在离屏/帧缓冲路径上做边缘平滑 */
int  fxtk_aa(void);
void fx_fill_polygon(const int16_t *pts, int n);
void fx_draw_text(int x, int y, const char *s);
void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg);
int  fx_text_width(const char *s);
void fx_canvas_begin(fx_widget_t *cv);
void fx_canvas_end(void);
int  fx_canvas_enable_buf(fx_widget_t *cv);
void fx_canvas_size(fx_widget_t *cv, int *w, int *h);   /* 画布本地宽高 (canvas 回调内取 cw/ch) */
void fx_canvas_clear(fx_widget_t *cv, fx_color_t color); /* 一键清空画布到 color (省去重复铺底样板) */

/* 桌面扩展: 键盘事件 */
typedef struct { char utf8[64]; int key; int down; int mod; } fx_keyev_t;
enum { FX_KEY_BACKSPACE = 8, FX_KEY_RETURN = 13, FX_KEY_ESCAPE = 27,
       FX_KEY_LEFT = 1, FX_KEY_RIGHT = 2, FX_KEY_HOME = 3, FX_KEY_END = 4,
       FX_KEY_UP = 5, FX_KEY_DOWN = 6, FX_KEY_DELETE = 127 };

typedef struct {
    uint32_t width, height;
    int  (*init)(void);
    void (*set_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*push_pixels)(const uint32_t *px, uint32_t n);
    void (*hold_begin)(void);
    void (*hold_end)(void);
    void (*fill_rect)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);
    int  (*touch_read)(int *x, int *y, int *pressed);
    int  (*key_read)(fx_keyev_t *ev);
    void (*clip_set)(const char *s);          /* 可选: 系统剪贴板 */
    const char *(*clip_get)(void);
    void (*set_title)(const char *s);      /* 可选: 窗口标题 */
    int  (*wheel_read)(int *x, int *y, int *dy);  /* 可选: 滚轮 */        /* 可选: 键盘事件 */
    void (*blit_img)(const uint32_t *px,int w,int h,int dx,int dy,int dw,int dh,int dark); /* GPU缩放blit */
    void (*set_clip_rect)(int x1,int y1,int x2,int y2);
    void (*fill_tri)(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t c); /* GPU三角 */
    void (*draw_line)(int x1,int y1,int x2,int y2,uint32_t c); /* GPU折线 */
    void (*blit_tex)(void *tex,int sx,int sy,int sw,int sh,int dx,int dy); /* GPU文字blit(src+dst) */
    void (*blit_img_rot)(const uint32_t *px,int w,int h,int cx,int cy,int dw,int dh,double ang); /* GPU旋转blit */
} fx_driver_t;

void fx_init(const fx_driver_t *drv);
void fx_poll(void);
uint16_t fx_width(void);
uint16_t fx_height(void);
void fx_set_autorepaint(int on);
void fx_set_touch_debug(int on);
void fx_set_bg(fx_color_t c);
void fx_set_global_background(fx_colorx_t c);
void fx_set_dark_theme(int dark);
int  fx_is_dark_theme(void);
fx_color_t fx_colorx_current(fx_colorx_t c);
void fx_set_window_title(const char *s);
void fx_set_grid_lines(int on);   /* 调试: 显示网格线 */
int fxtk_grid_lines_on(void);  /* 运行时改窗口标题 */
void fxtk_set_fps_debug(int on); /* 左下角 FPS 调试信息 (默认关) */
int fxtk_widget_count(void);   /* 当前存活的控件总数 */
int fxtk_fps(void);            /* 驱动刷新率 (帧/秒) */
void fx_widget_fix(fx_widget_t *w, int x1, int y1);  /* 固定坐标模式: 防布局重算, 记录基准 */
fx_color_t fx_get_bg(void);

/* 核心丝滑滚动 */
int  fx_scroll_update(fx_widget_t *w,int content_h);
void fx_scrollbar_draw(fx_widget_t *w,int off,int content_h);
void fx_set_scroll(fx_widget_t *w,int off);   /* v2.3 统一滚动: 设目标(拖滚动条用), 走同套缓动 */
void fx_canvas_set_buf(fx_widget_t *w,int on);

/* ---- extra: 多尺寸文字 / 列表 / 下拉 ---- */
void *fxtk_font_size(int size);
void fxtk_font_set_size(int size);   /* 固定当前字号, 避免遗留 g_font 影响文字/光标宽度 */

int  fxtk_text_width_size(int size, const char *t);
void fxtk_draw_text_size(int size, int x, int y, const char *t, fx_color_t fg, fx_color_t bg);
fx_widget_t *fx_list_new(const char *r1, const char *r2);
void fx_list_add(fx_widget_t *w, const char *t);
void fx_list_set_cb(fx_widget_t *w, void (*cb)(fx_widget_t*, void*));
int  fx_list_sel(fx_widget_t *w);
void fx_list_clear(fx_widget_t *w);   /* v2.3: 清空列表(供重建/删除) */
fx_widget_t *fx_drop_new(const char *r1, const char *r2);
void fx_drop_add(fx_widget_t *w, const char *t);
void fx_set_fontsize(fx_widget_t *w, int size);
void fx_set_align(fx_widget_t *w, int a);
fx_widget_t *fx_list_new_p(const char *r1, const char *r2, int pg);
fx_widget_t *fx_drop_new_p(const char *r1, const char *r2, int pg);
int  fxtk_ui_scale(void);
void fxtk_set_ui_scale_cap(int p);   /* 行高/字号缩放上限% */          /* UI 缩放百分比, 480 设计宽=100 */
int  fxtk_font_height(int size);  /* 缩放后字高 */
int  fxtk_drv_width(void);
int  fxtk_drv_height(void);

#ifdef __cplusplus
}
#endif

#endif /* FXTK_H */
