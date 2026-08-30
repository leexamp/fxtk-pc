# fxtk API 参考

> 以 `components/fxtk/fxtk.h` 为最终清单。颜色为 24bit RGB（`0xRRGGBB`，`uint32_t`）。
> 完整教程见 `guide.md`。

## 属性宏

| 宏 | 说明 |
|---|---|
| `pixel("x1,y1","x2,y2")` | 480×272 设计坐标，等比缩放 |
| `percent("a,b","c,d")` | **0.0~1.0 浮点**，相对父控件 |
| `grid("name",r1,c1,r2,c2)` | 命名网格的格子区间；`grid("name")` 铺满 |
| `title(s)` / `name(s)` | 文本 / 控件名（全局唯一） |
| `call(fn)` | 回调 `void fn(fx_widget_t*,void*)` |
| `color(c)` / `fgcolor(c)` | 背景 / 前景色 |
| `line(n)` | 字号 |
| `row(n)` | 对齐 0左 1中 2右 |
| `value(n)` | 初始值 0~100 |
| `maxlen(n)` | 输入框限长（`fxtk_desktop.h`） |
| `anim(n)` | 1=每帧重绘 |
| `page(n)` | 挂到 tab 第 n 页 |
| `border(n)` / `radius(n)` | 边框 / 圆角 |
| `dense()` | grid 密集模式 |
| `image(img)` | image 控件初始图 |

## 控件创建

```c
fx_widget_t *fx_label_new(...);     /* 文本 */
fx_widget_t *fx_button_new(...);    /* 按钮 */
fx_widget_t *fx_canvas_new(...);    /* 画布(立即式回调) */
fx_widget_t *fx_slider_new(...);    /* 滑杆 */
fx_widget_t *fx_progress_new(...);  /* 进度 */
fx_widget_t *fx_checkbox_new(...);  /* 复选 */
fx_widget_t *fx_textedit_new(...);  /* 输入框 (fxtk_desktop.h) */
fx_widget_t *fx_list_new_p("r1","r2",pg);    /* 列表 (pg=标签页页码, -1=无) */
fx_widget_t *fx_drop_new_p("r1","r2",pg);    /* 下拉 (pg=标签页页码, -1=无) */
fx_widget_t *fx_tab_new(...);  fx_widget_t *fx_panel_new(...);
fx_widget_t *fx_grid_map(...); fx_widget_t *fx_image_new(...);
```

示例：

```c
fx_button_new(pixel("340,40","400,70"), name("br_r"), page(6),
              title("红"), color(FX_RGB(244,67,54)), call(on_brush));
fx_widget_t *d = fx_drop_new_p("220,106","360,132", 10);
fx_drop_add(d,"字体: 小"); fx_list_set_cb(d, on_drop);
```

## 通用操作

```c
fx_widget_t *fx_find(const char *name);
void fx_parent(fx_widget_t *p);                 /* 设默认父, NULL=根 */
void fx_set_title(fx_widget_t*, const char*);
void fx_set_color_w(fx_widget_t*, fx_color_t);
void fx_set_fgcolor(fx_widget_t*, fx_color_t);
void fx_set_value(fx_widget_t*, int v);         /* 0~100 */
int  fx_get_value(const fx_widget_t*);
void fx_set_cb(fx_widget_t*, void(*)(fx_widget_t*,void*), void*);
void fx_set_visible(fx_widget_t*, int);
void fx_widget_rect(fx_widget_t*, int*,int*,int*,int*);
void fx_widget_set_rect(fx_widget_t*, int x1,int y1,int x2,int y2);
void fx_widget_fix(fx_widget_t*, int x,int y);  /* 固定坐标, 防布局复位 */
void fx_repaint(void);
void fx_repaint_rect(int x1,int y1,int x2,int y2);
void fx_delete(fx_wptr(w));                     /* 删除控件 (用 fx_wptr 或 grid(name) 定位) */
int  fxtk_widget_count(void);                   /* 存活控件总数 */
int  fxtk_fps(void);                            /* 帧率 */
int  fxtk_ui_scale(void);                       /* 480宽=100 */
void fx_set_max_scale(float f);                 /* 控件缩放上限(倍) */
```

## 绘制原语（canvas 回调内，本地坐标）

```c
void fx_set_color(fx_color_t);
void fx_fill_rect(int x1,int y1,int x2,int y2);
void fx_fill_rect_gradient(int x1,int y1,int x2,int y2,fx_color_t c1,fx_color_t c2,int vertical); /* v2.2 渐变, vertical=1上下 */
void fx_draw_rect(int x1,int y1,int x2,int y2);
void fx_draw_hline(int x1,int x2,int y);
void fx_draw_vline(int x,int y1,int y2);
void fx_draw_line(int x1,int y1,int x2,int y2);
void fx_draw_circle(int cx,int cy,int r);       void fx_fill_circle(...);
void fx_draw_ellipse(int cx,int cy,int rx,int ry);  void fx_fill_ellipse(...);
void fx_draw_arc(int cx,int cy,int r,int a1,int a2); void fx_fill_arc(...);
void fx_draw_rect_round(int x1,int y1,int x2,int y2,int r);
void fx_fill_rect_round(int x1,int y1,int x2,int y2,int r);
void fx_draw_triangle(int x1,int y1,int x2,int y2,int x3,int y3);
void fx_fill_triangle(int x1,int y1,int x2,int y2,int x3,int y3);
void fx_draw_polygon(const int16_t *pts, int n);
void fx_fill_polygon(const int16_t *pts, int n);
void fx_draw_text_c(int x,int y,const char*,fx_color_t fg,fx_color_t bg);
void fxtk_draw_text_size(int size,int x,int y,const char*,fx_color_t fg,fx_color_t bg);
int  fx_text_width(const char*);
int  fxtk_text_width_size(int size,const char*);
void fx_set_clip(int x1,int y1,int x2,int y2);
void fx_reset_clip(void);
```

## 画布（v2.2 便捷 API）

canvas 回调内以本地坐标 (0,0)~(cw-1,ch-1) 绘制；图元见上节。

```c
void fx_canvas_begin(fx_widget_t *cv);   /* 进入画布绘制 (框架自动调用, 通常无需手动) */
void fx_canvas_end(void);
int  fx_canvas_enable_buf(fx_widget_t *cv);          /* 开启离屏缓冲, 0 成功 */
void fx_canvas_set_buf(fx_widget_t *cv, int on);     /* 开关离屏缓冲 */
void fx_canvas_size(fx_widget_t *cv, int *w, int *h);/* 取画布本地宽高 (cb 内代替 fx_widget_rect 样板) */
void fx_canvas_clear(fx_widget_t *cv, fx_color_t c); /* 一键清底到颜色 c */
```

## 文本

```c
void fx_set_fontsize(fx_widget_t*, int size);
void fx_set_align(fx_widget_t*, int a);   /* 0/1/2 */
void *fxtk_font_size(int size);           /* 取字号字体句柄 */
int  fxtk_font_height(int size);
```

## 输入

```c
void fx_touch_state(int *x,int *y,int *pressed);  /* 悬停也实时 */
fx_widget_t *fx_pressed(void);
fx_keyev_t fx_last_key(void);   /* .utf8 / .key(FX_KEY_*) */
int  fx_wheel_take(fx_widget_t*);         /* 画布滚轮增量 */
void fx_textedit_set_readonly(fx_widget_t*, int);
void fx_set_focus(fx_widget_t*);  fx_widget_t *fx_get_focus(void);
```

## 核心滚动（自绘画布/列表一行接入）

```c
int  fx_scroll_update(fx_widget_t *w, int content_h);
/* 更新滚动状态并返回当前偏移(整数):
 *  - 滚轮增量累积为目标像素, 每帧 25% 插值逼近 (rc 手感, 无惯性滞留)
 *  - 内部状态池支持多控件并行滚动互不干扰
 *  - 偏移变化时自动请求重绘 (静止零重绘)
 *  - 同时记录 w->content_h, 支持滚动条滑块拖拽
 * 自绘画布回调里: int off = fx_scroll_update(w, total); 然后按 off 绘制可见行 */
void fx_scrollbar_draw(fx_widget_t *w, int off, int content_h);
/* 库滚动条: 轨道 + 滑块, 跟随 off 偏移 */
```

画布示例（`examples/ex05_scroll.c` 完整可运行）：

```c
static void on_view(fx_widget_t *w, void *ud) {
    int off = fx_scroll_update(w, 100 * 36);   /* 100 行 x 36px */
    /* 按 off 绘制可见行 ... */
    fx_scrollbar_draw(w, off, 100 * 36);
}
```

## 字体

```c
void fxtk_font_set_size(int size);   /* 固定当前字号 (文字/宽度测量用同一字体) */
```


## 列表/下拉数据

```c
void fx_list_add(fx_widget_t*, const char*);
void fx_drop_add(fx_widget_t*, const char*);
void fx_list_set_cb(fx_widget_t*, void(*)(fx_widget_t*,void*));
int  fx_list_sel(fx_widget_t*);           /* 选中行号 */
```

## 图片/特效

```c
fx_image_t *fx_image_create(int w,int h);
void fx_image_free(fx_image_t*);
void fx_image_set_px(fx_image_t*, int x,int y, fx_color_t);
void fx_draw_image(fx_image_t*, int x,int y,int dw,int dh);
void fx_draw_image_ex(fx_image_t*, int x,int y,int dw,int dh,int dark);
void fx_draw_image_rot(fx_image_t*, int cx,int cy,int deg,int pct);
void fx_image_flip_x(fx_image_t*);  void fx_image_flip_y(fx_image_t*);
void fx_image_grayscale(fx_image_t*);
void fx_image_tint(fx_image_t*, fx_color_t c, int amount);   /* 0~255 */
void fx_image_brightness(fx_image_t*, int delta);             /* -255~255 */
void fx_set_image(fx_widget_t*, fx_image_t*);                 /* image 控件 */
void fx_image_set_zoom(fx_widget_t*, int pct);                /* 10~400 */
```

## 系统

```c
void fx_init(const fx_driver_t *drv);
void fx_poll(void);
uint16_t fx_width(void);  uint16_t fx_height(void);
void fx_set_bg(fx_color_t c);   fx_color_t fx_get_bg(void);
void fx_set_window_title(const char*);
void fx_set_touch_debug(int on);
void fx_set_grid_lines(int on);
void fxtk_set_fps_debug(int on);   /* 左下角 FPS 角标 (默认关, demo 才开) */
int  fx_widget_type(const fx_widget_t*);
const char *fx_widget_title(const fx_widget_t*);
/* 主题: 浅/深两套配色 */
void fx_set_dark_theme(int dark);          /* 1=深色 */
int  fx_is_dark_theme(void);
void fx_set_global_background(fx_colorx_t c);
fx_color_t fx_colorx_current(fx_colorx_t c);
```

## 默认配色速查

不指定颜色即可用的默认值：

| 控件 | 背景 | 前景 |
|---|---|---|
| 窗口 | 245,245,245 | — |
| label / checkbox | 透明 | 40,40,40 深字 |
| 按钮 | 33,150,243 蓝 | 白 |
| grid / panel / tab | 245,245,245 | 浅灰网格线 |
| slider / progress | 76,175,80 绿 | 浅灰轨道 |
| canvas | 245,245,245 | — |
| textedit | 白 | 黑 |
