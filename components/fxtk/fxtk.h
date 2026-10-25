/**
 * fxtk.h — FX Toolkit 公共头文件
 */
#ifndef FXTK_H
#define FXTK_H

/* 框架版本 (发布标记) */
#define FXTK_VERSION "2.5.5"   /* 窗口标题/文档/发布包统一取此值, 避免各处手写版本号漂移 */

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

/* v2.6: RGBA —— 高 8 位是 alpha。alpha=0 视为"不透明", 与旧的 24 位颜色完全兼容;
 * 需要半透明时用 FX_RGBA(...) 显式给 alpha (1..255)。
 * 支持 alpha 的路径: GPU 填充/线段/圆角矩形/图片/文字贴图(整个 sokol 管线本来就开了 blend)。 */
#define FX_RGBA(r, g, b, a) \
    ((fx_color_t)(((fx_color_t)(a) << 24) | (((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF)))
/* 文字专用: 让字形背景透明 (只在 stb 字体驱动里识别) */
#define FX_TEXT_NOBG ((fx_color_t)0x00FFFFFFu)
static inline int fx_color_alpha(fx_color_t c) { unsigned a = (unsigned)(c >> 24); return a ? (int)a : 255; }
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
#define FX_POS_FLEX  4   /* v2.5: 坐标由 fx_flex_apply 写入, 布局期不重算 */
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
/* v2.5.2: 多窗口控件根 (单窗口不用) —— 见 docs/multi-window.md */
fx_widget_t *fx_root_new(void);              /* 新建一棵独立的控件树根 (不挂到当前根) */
void         fx_root_select(fx_widget_t *r); /* 切换当前活动根; NULL = 回到缺省根 */
fx_widget_t *fx_root_active(void);           /* 当前活动根 */
void fxtk_unlink(fx_widget_t *w);   /* v2.6: 从父链表摘除 */
void fxtk_raise(fx_widget_t *w);    /* v2.6: 提到最前(最后绘制) */
void fxtk_lower(fx_widget_t *w);    /* v2.6: 压到最底(最先绘制) */
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

/* ==================== 设计稿坐标空间（v2.5.3） ====================
 * pixel("x1,y1","x2,y2") 里的数是【设计稿坐标】, 不是设备像素 —— 框架按设计稿尺寸把
 * 它等比缩放到当前窗口。默认设计稿 480×272:
 *
 *     fx_set_design_pixel(1280, 720);              // 换成自己的 1280×720 设计稿
 *     fx_button_new(pixel("980,600","1200,680"), name("ok"), title("确定"), call(on_ok));
 *
 * 之后所有 pixel() 坐标直接按设计稿量, 不用再手算 480/1280 之类的系数;
 * 文字/控件缩放比例(fxtk_ui_scale)也同步以设计稿宽为 100%。
 * 改完立即重排/重绘; 未 fx_init 前调用则首帧生效。 */
void fx_set_design_pixel(int w, int h);   /* 设计稿尺寸, 合法范围 16..8192; 默认 480×272 */
void fx_get_design_pixel(int *w, int *h); /* 取当前设计稿尺寸(可传 NULL) */
void fxtk_apply_fit(fx_widget_t *w);
void fx_touch_press(int x, int y);
void fx_touch_release(int x, int y);
void fx_touch_move(int x, int y);
/* v2.5.5: 触摸屏能力 —— 拖动滚动默认只在检测到触摸屏时启用(桌面鼠标拖动不进滚动路径)。
 * fx_touch_capable(): 当前是否按触摸屏对待; fx_set_touch_mode(-1 自动 / 0 强制关 / 1 强制开)。 */
int  fx_touch_capable(void);
void fx_set_touch_mode(int on);

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
void fx_set_aa(int level);
/* ================= v2.4 抗锯齿分两层, 互不牵连 =================
 *  ① 画布 CPU AA (s_aa): 开时 line/circle/rect 等矢量图元在【离屏画布】上做边缘混合。
 *     代价大(强制离屏 + 逐像素混合, v2.2 实测 ~10ms/帧级), 因此默认仍关; fx_set_aa(level≥1) 打开。
 *  ② 控件/图元 GPU AA (s_widget_aa): 圆角矩形/描边由驱动的 SDF 钩子一次画完, 片元按到边界距离
 *     求覆盖度 → 边缘天然平滑, 且比逐行填充更快。默认档 1(有钩子时开)。
 *  档位: 0=关, 1=SDF(默认), 2=1+图元羽化(线段/圆/弧)。掉帧时由驱动调 fx_aa_autodegrade() 降档。
 *  fx_set_aa(level) 同时设置两层(保持 v2.2 兼容); 只想动 GPU 层用 fx_set_widget_aa()。 */
/* v2.4.3: 全局动画开关(默认关, 保证无头测试/金图确定性)。
 * 打开后, 支持动画的控件会做过渡(如标签药丸淡入、进度条数值缓动);
 * 单个控件用 anim(0) 显式关闭。 */
void fx_animation(int on);
int  fx_animation_enabled(void);

void fx_set_widget_aa(int level);   /* 只设 GPU 控件层档位 0/1/2 */
/* v2.4.1: 把输入法候选窗要贴的位置(文本框光标, 屏幕坐标)告诉后端。
 * 驱动实现 ime_pos 钩子即可(SDL 版走 SDL_SetTextInputRect, sokol 版走 XIM 的 XNSpotLocation);
 * 无该钩子/无输入法时空转, 不影响其它平台。 */
void fx_set_ime_pos(int x, int y);
int  fx_widget_aa_level(void);
int  fx_aa_autodegrade(void);       /* 供驱动在掉帧时调用: 降一档并返回新档位(已到 0 则返回 0) */
int  fxtk_aa(void);                 /* 画布 CPU AA 当前开关 */
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

/* ================= v2.4 canvas 变换栈 (2D 仿射) =================
 * push 后, 立即模式图元的坐标会经过变换:
 *   像素/线段 → 端点变换; 矩形填充 → 实心四边形(可旋转/斜切); 图片 → 透视四边形。
 * 语义: p' = M·p, M 为 [[a,c,e],[b,d,f],[0,0,1]]; 连续 push 会与栈顶复合(新变换后应用)。
 * 栈深 8(满则覆盖栈顶); 每帧自动复位, 但成对 push/pop 仍是好习惯。
 * 例: 旋转 θ → fx_canvas_push_affine(cos,-sin 等); 平铺 → 多次 push 平移后画图再 pop。 */
void fx_canvas_push_affine(float a, float b, float c, float d, float e, float f);
void fx_canvas_pop_affine(void);
void fx_transform_reset(void);
int  fx_transform_depth(void);
/* 把点过一遍当前变换 (栈空则原样返回) */
void fx_canvas_transform_point(float x, float y, float *ox, float *oy);
/* 实心四边形填充 (四角顺时针 xy8), 与 fx_draw_image_quad 同一套单应机制 */
void fx_fill_quad(const float *xy8);

/* ================= v2.4 GPU 实时光线步进 =================
 * 有 GPU 且后端提供 raymarch 钩子时, 直接在指定矩形里由片元着色器算光线步进 ——
 * 不占 CPU 像素缓冲、不做回读。无该能力时返回 0, 调用方回退到 CPU 光追。 */
int  fx_raymarch_available(void);
int  fx_quad_warp_gpu(void);   /* v2.4: 四边形形变是否由驱动的 GPU 钩子接管 (HUD 显示用) */
void fx_draw_raymarch(float time, int x, int y, int w, int h);

/* 桌面扩展: 键盘事件 */
typedef struct { char utf8[64]; int key; int down; int mod; } fx_keyev_t;
enum { FX_KEY_BACKSPACE = 8, FX_KEY_RETURN = 13, FX_KEY_ESCAPE = 27,
       FX_KEY_LEFT = 1, FX_KEY_RIGHT = 2, FX_KEY_HOME = 3, FX_KEY_END = 4,
       FX_KEY_UP = 5, FX_KEY_DOWN = 6, FX_KEY_DELETE = 127 };

typedef struct {
    int width, height;
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
    int  (*read_pixels)(uint32_t *dst,int w,int h);   /* v2.4 可选: 回读当前帧 (截图/金图回归); dst 填 0xRRGGBB 或 0xAARRGGBB */
    void (*draw_image_quad)(const uint32_t *px,int w,int h,const float *xy8,int bilinear); /* v2.4 可选: GPU 真透视四边形 (无则走 CPU 逆单应) */
    void (*raymarch)(float time,int x1,int y1,int x2,int y2); /* v2.4 可选: GPU 实时光线步进到指定矩形 (sokol) */
    /* v2.4 可选: GPU SDF 圆角矩形填充/描边 (片元按到边界距离羽化 → 边缘抗锯齿, 每控件一次 draw) */
    void (*fill_rect_round)(int x1,int y1,int x2,int y2,int r,uint32_t c);
    void (*stroke_rect_round)(int x1,int y1,int x2,int y2,int r,int bw,uint32_t c);
    /* v2.4 可选: GPU 羽化线段 (宽度 w, 片元按到线心距离羽化; 无则回退双三角硬边) */
    void (*draw_line_aa)(int x1,int y1,int x2,int y2,int w,uint32_t c);
    void (*ime_pos)(int x,int y);   /* v2.4.1 可选: 输入法候选窗位置(留给后端) */
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
/* v2.4: 回读当前帧并存为 PNG (依赖驱动的 read_pixels 钩子; 无该钩子返回 0)。
 * 用途: 截图画廊 / CI 金图回归 / 用户级截图按钮。 */
int fx_screenshot(const char *path);
void fx_widget_fix(fx_widget_t *w, int x1, int y1);  /* 固定坐标模式: 防布局重算, 记录基准 */
fx_color_t fx_get_bg(void);

/* ==================== v2.5.5: 统一滚动 API ====================
 * 滚动容器(fx_scroll_new)、列表/下拉、输入框、自绘画布 共用同一套滚动模型:
 *   - 偏移是【像素】, 滚轮/触摸拖动/惯性/夹取/滚动条几何全部一致;
 *   - 触摸屏: 在内容上按住拖动 >8px 即接管为滚动(1:1 跟手), 松手带惯性, 不会误触发点击;
 *   - 自绘画布: 内容由回调里的 fx_scroll_update(w, total) 声明; 列表/输入框/容器内部已接好。
 * 旧 API(fx_scroll_update / fx_scrollbar_draw / fx_set_scroll)保留兼容。 */
int  fx_scroll_offset(fx_widget_t *w);        /* 当前像素偏移 */
int  fx_scroll_max(fx_widget_t *w);           /* 最大偏移(内容高-视口高, 已含夹取) */
int  fx_scroll_view_h(fx_widget_t *w);        /* 视口高(输入框已扣上下内边距) */
void fx_scroll_to(fx_widget_t *w, int off);   /* 设目标偏移(缓动) */
void fx_scroll_set(fx_widget_t *w, int off);  /* 立即定位(拖滚动条/回到顶部用) */
void fx_scroll_by(fx_widget_t *w, int dy);    /* 相对滚动(缓动) */
void fx_scroll_to_top(fx_widget_t *w);
void fx_scroll_to_bottom(fx_widget_t *w);
/* 兼容旧 API */
int  fx_scroll_update(fx_widget_t *w,int content_h);   /* 每帧调用: 消费滚轮/推进缓动, 返回像素偏移 */
void fx_scrollbar_draw(fx_widget_t *w,int off,int content_h);   /* 画统一滚动条 */
void fx_set_scroll(fx_widget_t *w,int off);   /* v2.3 旧名: 等价 fx_scroll_set(立即) */
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
void fxtk_set_ui_scale_cap(int p);   /* 行高/字号缩放上限% */          /* UI 缩放百分比, 设计稿宽=100% */
int  fxtk_font_height(int size);  /* 缩放后字高 */
int  fxtk_drv_width(void);
int  fxtk_drv_height(void);

/* ==================== v2.5: 并行 for（CPU，不是 GPU） ====================
 * 【先把名字说清 —— 它叫 par 而不是 gpu，因为按"gpu"叫就是名不副实】
 *   ① GPU 上跑的是 GLSL/SPIR-V；C 代码块没法直接变成 GPU 指令 —— 那要先写个
 *      C 子集编译器，属于重写 nvcc，不是"后端升级"。
 *   ② sokol_gfx 只有 vertex/fragment 管线，【没有 compute shader】
 *      （无 sg_dispatch 之类）—— 这是它的设计边界，不是版本问题。
 *   所以本模块是【CPU 多线程】。想要真 GPU 并行，得换掉 sokol_gfx 走裸 GL 4.3
 *   compute / Vulkan，代价是丢掉 ESP32 与 SDL2 后端的对等支持。
 *
 * 【两种用法】
 *   语法糖（串行，零开销，全平台 —— 只是让循环读起来像一句话）:
 *       fx_par_for(n) { a[fx_i] = fx_i * fx_i; }
 *   真并行（要回调，因为宏没法把任务切给线程）:
 *       static void fill(int lo, int hi, void *ud) {
 *           int *a = ud;  for (int i = lo; i < hi; i++) a[i] = i * i;
 *       }
 *       fx_parallel_for(n, fill, a);
 *
 * 【⚠️ 它不会让"压测页"更快，别指望错方向】
 *   压测页实测 59~108 fps 的瓶颈是【每帧整画布重绘 + 上传约 7.7MB 像素】
 *   （见 CHANGELOG v2.4.3 的结论），那是光栅化与传输，不是算力。
 *   并行循环帮的是【你自己的数据处理】，不是控件绘制。
 *
 * 【为什么不用 OpenMP / C11 threads】
 *   OpenMP 会引入 libgomp 依赖，破坏"零第三方动态库"；C11 <threads.h> 在 mingw
 *   与部分嵌入式工具链上支持不全。所以直接用 pthreads / Win32 线程，
 *   其余平台（含 ESP32）退化为串行 —— 接口与语义不变，只是不并行。
 */
#define fx_par_for(cnt) for (int fx_i = 0, fx_n_ = (int)(cnt); fx_i < fx_n_; fx_i++)

void fx_parallel_for(int n, void (*body)(int lo, int hi, void *ud), void *ud);
/* 逻辑核数用已有的 fx_cpu_count()(声明在 fxtk_backends.h) —— 不要在这里重复声明,
 * v2.5 初版曾在这里再声明一次并在 fxtk_par.c 里再实现一次, 直接撞出
 * "multiple definition of fx_cpu_count" 链接错误。 */
int  fx_parallel_threads(void);       /* 当前设定 (0 = 自动) */
void fx_parallel_set_threads(int n);  /* 0 = 自动; 1 = 强制串行 */

/* ==================== v2.5: 触摸手势（单指） ====================
 * 【用法】驱动已有 touch_read 的话, 在 fx_poll() 之后取空事件队列即可:
 *     fx_gesture_ev_t ev;
 *     while (fx_gesture_poll(&ev)) {
 *         if (ev.type == FX_GEST_SWIPE_LEFT) 切到上一页();
 *         if (ev.type == FX_GEST_DOUBLE_TAP) 重置();
 *     }
 * 【不用改驱动】框架已在 fx_poll() 里把触摸样本喂进来了; 本组 API 只负责识别。
 *
 * 【⚠️ 只支持单指】当前驱动接口 touch_read(x,y,pressed) 只给一个触点,
 *   所以捏合缩放(pinch)/双指旋转【没有实现】—— 那不是"以后再说", 是接口给不出
 *   第二个触点。要支持必须先扩展驱动接口。别把本模块当多指方案。
 *
 * 【单击会延迟 g_double_tap_ms 才报】为了区分单击与双击, TAP 必须等双击窗口过去
 *   才能确定。所以"要立刻响应的按钮"应当用控件自身的按下/抬起, 而不是等 TAP。
 *   这是个真实取舍, 不是 bug。 */
typedef enum {
    FX_GEST_NONE = 0,
    FX_GEST_TAP,           /* 单击(双击窗口后结算) */
    FX_GEST_DOUBLE_TAP,
    FX_GEST_LONG_PRESS,
    FX_GEST_DRAG_BEGIN,
    FX_GEST_DRAG_MOVE,     /* 拖拽中会持续产生 */
    FX_GEST_DRAG_END,
    FX_GEST_SWIPE_LEFT,    /* 四向滑动(快且远) */
    FX_GEST_SWIPE_RIGHT,
    FX_GEST_SWIPE_UP,
    FX_GEST_SWIPE_DOWN
} fx_gesture_type_t;

typedef struct {
    fx_gesture_type_t type;
    int x, y;          /* 事件发生位置(拖拽类为当前位置) */
    int dx, dy;        /* 相对按下点的位移 */
    int duration_ms;   /* 从按下到本事件的耗时 */
} fx_gesture_ev_t;

void fx_gesture_feed(int x, int y, int pressed, uint32_t now_ms);  /* 框架内部调用 */
int  fx_gesture_poll(fx_gesture_ev_t *ev);   /* 取一条, 无则 0; 建议 while 取空 */
int  fx_gesture_pending(void);               /* 队列里还剩几条 */
int  fx_gesture_active(void);                /* 手指是否正按着 */
int  fx_gesture_dropped(void);               /* 调试: 因队列满被丢弃的事件数 */
void fx_gesture_reset(void);
const char *fx_gesture_name(int type);       /* 中文名, 便于日志/调试页 */
/* 调阈值; 传 <=0 的项保持原值。默认: 长按500ms 点按300ms/12px 滑动40px/600ms 双击300ms */
void fx_gesture_config(int long_press_ms, int tap_max_ms, int tap_max_dist,
                       int swipe_min_dist, int double_tap_ms);
void fx_gesture_config_default(void);

/* ==================== v2.5: 弹性布局（flex 子集） ====================
 * 【用法】容器尺寸照旧用 pixel/percent 定, 子件先随便给个矩形(它会被覆盖),
 * 在容器尺寸确定后调一次:
 *     fx_flex_apply_simple(panel, FX_FLEX_ROW, 8, 6,
 *                          FX_FLEX_SPACE_BETWEEN, FX_FLEX_ALIGN_CENTER);
 *                                        // 8 = 子件间距 gap, 6 = 容器内边距 pad
 * 需要按权重分配宽度时用完整版：
 *     fx_flex_item_t it[3] = {
 *         { .size = 60,            .grow = 0 },   // 固定 60
 *         { .size = 60,            .grow = 1 },   // 按比例吃掉剩余
 *         { .size = -1, .grow = 2, .min_size = 40 },
 *     };
 *     fx_flex_apply(panel, it, 3, FX_FLEX_ROW, 8, 6,
 *                   FX_FLEX_START, FX_FLEX_ALIGN_STRETCH);
 *
 * 【什么时候调】它不是"每帧自动跑"的, 而是一次性把坐标写死并把子件标成
 * FX_POS_FLEX(布局期不再重算)。所以: 窗口缩放 / 容器改变尺寸 / 增删子件之后
 * 要【再调一次】, 否则坐标是旧的。
 *
 * 【诚实边界 —— 没做的部分不装做过了】
 *   ① 不收缩: 空间不够时【不压缩】子件, 直接溢出(不做 flex-shrink)。
 *   ② 不换行(不做 flex-wrap)。
 *   ③ 容器尺寸要调用方给; 不做嵌套 flex 的自动尺寸推导。
 *   ④ 一次最多处理 64 个直接子件(超出部分不排, 但不会越界写内存)。 */
typedef enum { FX_FLEX_ROW = 0, FX_FLEX_COL = 1 } fx_flex_dir_t;
typedef enum {
    FX_FLEX_START = 0, FX_FLEX_CENTER, FX_FLEX_END,
    FX_FLEX_SPACE_BETWEEN, FX_FLEX_SPACE_AROUND
} fx_flex_justify_t;
typedef enum {
    FX_FLEX_ALIGN_START = 0, FX_FLEX_ALIGN_CENTER, FX_FLEX_ALIGN_END, FX_FLEX_ALIGN_STRETCH
} fx_flex_align_t;

typedef struct {
    int size;        /* 主轴基础尺寸(px); <=0 视为 0(靠 grow 撑开) */
    int grow;        /* 弹性系数; 0 = 不伸缩 */
    int min_size;    /* 夹紧下限; <=0 = 不限制 */
    int max_size;    /* 夹紧上限; <=0 = 不限制 */
    int cross_size;  /* 交叉轴尺寸; <=0 = 用容器可用交叉轴 */
} fx_flex_item_t;

/* 纯函数: 只算坐标, 不碰控件与驱动。返回实际排布的项数。
 * out_xy4 需要 4*n 个 int, 每项按 (x1,y1,x2,y2) 写入。 */
int  fx_flex_solve(int cx1, int cy1, int cx2, int cy2,
                   int dir, int gap, int pad, int justify, int align,
                   const fx_flex_item_t *items, int n, int *out_xy4);

/* 应用: 把容器【可见子件】按 items 求解并写入坐标(标记 FX_POS_FLEX) */
void fx_flex_apply(fx_widget_t *container,
                   const fx_flex_item_t *items, int n,
                   int dir, int gap, int pad, int justify, int align);
/* 常用形态: 用每个子件【当前矩形尺寸】当基础尺寸, grow=0 */
void fx_flex_apply_simple(fx_widget_t *container, int dir, int gap, int pad,
                          int justify, int align);

/* ⚠️⚠️ 顺序陷阱(v2.5 写演示时踩到, 务必先读) ⚠️⚠️
 *
 * items[i] 对应的是【遍历顺序】里的第 i 个可见子控件, 而本框架的子控件链表是
 * 【头插】的 —— fxtk_link() 里 child->sibling = parent->child; parent->child = child;
 * 于是 fx_flex_apply / layout_children / 绘制 全都按 child->sibling 迭代, 也就是
 * 【逆着创建顺序】。换句话说:
 *
 *      最后创建的控件 = 链首 = items[0]
 *      最先创建的控件 = 链尾 = items[n-1]
 *
 * 所以下面这段的直觉是【错的】:
 *      fx_canvas_new(...);   // 以为这是"第一个", 会被排到最前面
 *      fx_canvas_new(...);
 *      fx_flex_apply(root, items, 2, FX_FLEX_COL, ...);   // items[0] 其实给了第二个
 *
 * 想让画面顺序 = 书写顺序, 就要【倒着创建】。这是框架级的一致约定(不是 flex 独有的
 * 行为), 改它会翻转所有既有演示的层叠顺序, 所以这里【只记录、不改】。 */

/* ==================== v2.5: SVG 子集 → canvas ====================
 * 【用法】解析一次(init 里), 每帧在 canvas 回调里画(自动等比缩放+居中):
 *     static fx_svg_t *g_icon;
 *     static void on_canvas(fx_widget_t *w, void *ud) {
 *         int cw, ch; fx_canvas_size(w, &cw, &ch);
 *         fx_canvas_clear(w, FX_WHITE);
 *         fx_svg_draw(g_icon, 0, 0, cw, ch);
 *     }
 *     g_icon = fx_svg_load("icon.svg");     // 或 fx_svg_parse(text)
 * 析构时 fx_svg_free(g_icon)。
 *
 * 【支持】<svg> <g> <rect> <circle> <ellipse> <line> <polyline> <polygon> <path>;
 *   fill/stroke/stroke-width/transform(translate,scale,rotate,matrix);
 *   路径 M L H V C Q Z(含小写相对形式); 颜色 #rgb/#rrggbb/rgb()/颜色名/none。
 *
 * 【不支持(不装做支持)】圆弧 A/a、平滑曲线 S/s T/t、渐变/图案/滤镜、
 *   <text>/<tspan>、CSS style 与 <style> 块、<use>/<defs>/裁剪蒙版。
 *   遇到不认识的元素会【跳过并计数】, 用 fx_svg_skipped() 取, 便于知道图被简化了多少。
 *
 * 【近似之处】曲线用固定 12 段采样(不是自适应细分), 所以极大放大时能看到折线。 */
typedef struct fx_svg fx_svg_t;

fx_svg_t *fx_svg_parse(const char *text);
fx_svg_t *fx_svg_load(const char *path);        /* 走 fx_file_read, stub 内存 FS 也能用 */
void      fx_svg_free(fx_svg_t *s);
/* 在 (x,y,w,h) 这个框内等比缩放并居中绘制; 需在 canvas 回调内调用 */
void      fx_svg_draw(const fx_svg_t *s, int x, int y, int w, int h);
/* 以下三个用于自检/调试, 也是测试能断言解析结果的入口 */
int       fx_svg_shape_count(const fx_svg_t *s);
int       fx_svg_shape_points(const fx_svg_t *s, int i);   /* 第 i 个形状的点数; 越界返回 -1 */
/* 第 i 个形状的包围盒(已应用 transform); 越界返回 0。自检用它断言 transform 生效 */
int       fx_svg_shape_bbox(const fx_svg_t *s, int i,
                            float *x1, float *y1, float *x2, float *y2);
int       fx_svg_skipped(const fx_svg_t *s);               /* 被跳过的元素/指令数 */
int       fx_svg_size(const fx_svg_t *s, float *vw, float *vh);   /* viewBox 尺寸 */

#ifdef __cplusplus
}
#endif

#endif /* FXTK_H */
