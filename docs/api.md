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

---

# v2.4 新增与变更 API 速查

> 这一节是 v2.4 的新增面。旧 API 未变（向后兼容），改动的默认值/行为在条目里注明。

## 抗锯齿（两层，默认已开）

```c
void fx_set_widget_aa(int level);   /* 0=关, 1=控件层 SDF 圆角/描边(默认), 2=再加图元羽化线 */
int  fx_widget_aa_level(void);      /* 当前档位 */
int  fx_aa_autodegrade(void);       /* 驱动在掉帧时调用: 降一档并返回新档位 */
```
- 免改代码对比:启动时设 `FXTK_AA=0|1|2`。
- 无对应钩子的后端（SDL / ESP32 纯 CPU）自动回退到逐行填充，**API 不变**。

## canvas 变换栈

```c
void fx_canvas_push_affine(float a,float b,float c,float d,float e,float f);
void fx_canvas_pop_affine(void);
void fx_transform_reset(void);      /* 每帧自动复位 */
int  fx_transform_depth(void);      /* 深度上限 8, 满则覆盖栈顶 */
void fx_canvas_transform_point(float x,float y,float *ox,float *oy);
void fx_fill_quad(const float *xy8);/* 实心四边形(可旋转/斜切) */
```
push 之后:像素/线段按端点过变换，**矩形填充变实心四边形**，**图片走透视四边形**。

## 图片四边形形变（真透视）

```c
void fx_draw_image_quad(const fx_image_t *img, const float *xy8);   /* 四角顺时针, 左上起 */
int  fx_quad_warp_gpu(void);        /* 形变是否由驱动 GPU 钩子接管(供 HUD 显示) */
/* 工具(想自建管线时用) */
int  fx_quad_homography(const float *src8, const float *dst8, float m[9]);
int  fx_mat3_invert(const float *m, float out[9]);
int  fx_quad_corner_weights(const float *xy8, float d4[4]);  /* 每角透视权重: 写进 gl_Position.w */
```
- GPU 路径单次 draw、透视校正插值（**没有两个三角形各做仿射的对角缝**）。
- 退化/自交四边形一律回退包围盒映射（**GPU/CPU 两条路径都有这道闸**）。

## GPU 实时光线步进

```c
int  fx_raymarch_available(void);
void fx_draw_raymarch(float time, int x, int y, int w, int h);
```

## 输入法（Linux/X11，v2.4.3）

```c
void fx_set_ime_pos(int x, int y);  /* 把文本框光标的屏幕坐标报给后端, 让候选窗贴上去 */
```
- 驱动侧可选钩子 `fx_driver_t::ime_pos`；框架在 textedit 画光标时自动上报。
- sokol 版走 XIM（已给 vendored sokol_app 打补丁：`XOpenIM`/`XCreateIC`/事件循环 `XFilterEvent`/`Xutf8LookupString`）。

## 截图与图片

```c
int fx_screenshot(const char *path);                 /* 驱动 read_pixels 回读当前帧 → PNG */
int fx_img_load_file(const char *path, fx_img_t *out);/* PNG/JPEG/BMP/GIF/TGA/PNM */
```

## 日志与服务层

```c
void fx_log(fx_log_level_t lv, const char *fmt, ...);  /* 核心统一出口; ESP32 侧转发 esp_log_write */
void fx_log_set_level(fx_log_level_t lv);
fx_caps_t   fx_backend_caps(void);
int fx_backend_pick_file(char *out,int cap,const char *title,const char *ext_csv);
```
**核心不再包含任何平台日志头**（v2.4.3 起；PC 端原先靠 demo 的 `esp_log.h` 垫片，已删除）。

## 驱动接口（`fx_driver_t`）v2.4 新增的可选钩子

`fill_rect_round` / `stroke_rect_round` / `draw_line_aa` / `read_pixels` / `draw_image_quad` /
`raymarch` / `ime_pos` —— 全部可选，未实现即自动回退旧路径。

## 配置宏（编译期）

| 宏 | 作用 | PC 默认 | ESP32 默认 |
|---|---|---|---|
| `FX_MAX_WIDGETS` | 控件池 | **16384** | 4096 |
| `FX_MAX_SCROLL_STATES` | 并发滚动状态 | **64** | 8 |
| `FX_MAX_EXTRA_WIDGETS` | 列表/下拉等扩展控件槽 | **64** | 8 |
| `FX_MAX_SCROLL_STATES` 之外 | 均可 `-D` 覆盖 | | |
| `FXTK_WIDGET_*` | 按控件裁剪（v2.3）减体积 | 全开 | 按需关 |
| `FXTK_BACKEND_STUB` | 确定性后端（固定时钟/定种子随机/内存文件） | 关 | — |
| `FX_MAX_WIDGETS` 同族 | 池满时**明确告警**并提示改哪个宏 | | |

## 调试/取证开关（环境变量 + 构建目标）

| 开关 | 用途 |
|---|---|
| `FXTK_AA=0\|1\|2` | 覆盖抗锯齿档位 |
| `FXTK_RECTDBG=1` | 图元参数取证：越界坐标 / 面积异常 / 半径异常 / 图片角点 → 打印参数与裁剪区 |
| `FXTK_SDFNOMERGE=1` | 每段 SDF 独立成命令（A/B 判定"命令合并"因素） |
| `FXTK_IMEDBG=1` | 打印上报给输入法的光标坐标 |
| `FXTK_IMPORT=<路径>` | 免对话框导入图片（CI/无桌面环境） |
| `FXTK_NOVSYNC=1` | 关垂直同步 |
| `./build_dbg.sh` / `make fxtk_sim_dbg` | **取证版**：探针编进二进制，不依赖环境变量；启动会打一行自检 |
