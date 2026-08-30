# fxtk 完整指南（guide.md）

> 本指南覆盖 fxtk 的全部常用能力：从第一个窗口到自绘动画、数据控件、性能优化。
> 环境：纯 PC / Linux 模拟器（`demo-main/`），SDL2 渲染，已完全脱离 ESP32。
> 建议顺序：第 1~4 章 → 用 quickstart 跑通 → 第 5~9 章写实际页面 → 第 10~14 章深入。

---

## 目录

1. [总体模型](#1-总体模型)
2. [构建与运行](#2-构建与运行)
3. [坐标系统与布局](#3-坐标系统与布局)
4. [颜色系统](#4-颜色系统)
5. [控件创建（属性宏）](#5-控件创建属性宏)
6. [控件类型详解](#6-控件类型详解)
7. [容器与页面](#7-容器与页面)
8. [绘制原语（canvas 立即式绘制）](#8-绘制原语canvas-立即式绘制)
9. [输入：触摸/键盘/滚轮](#9-输入触摸键盘滚轮)
10. [运行时操作控件](#10-运行时操作控件)
11. [图片与特效](#11-图片与特效)
12. [文本与字体](#12-文本与字体)
13. [弹层与右键菜单](#13-弹层与右键菜单)
14. [重绘与性能模型](#14-重绘与性能模型)
15. [完整示例：从零写一个波形页](#15-完整示例从零写一个波形页)
16. [常见坑与最佳实践](#16-常见坑与最佳实践)

---

## 1. 总体模型

fxtk 是一个 **"保留式控件 + 立即式绘制"** 混合的轻量 GUI：

- **控件（widget）是保留式的**：创建一次，长期存在，由核心统一布局、统一绘制、统一输入分发。改文字、改矩形、改颜色，核心自动重绘。
- **画布（canvas）内容是立即式的**：给 canvas 挂一个回调，核心按需调用它，你在回调里用绘图原语直接"画"——没有场景图，没有状态同步，画什么就是什么。
- **单线程 + 驱动抽象**：核心不依赖操作系统，只依赖一个显示/输入驱动（`fx_sdl_driver`）。换驱动即可移植到其他平台。

理解这个模型只需要记住一件事：

> **控件管"有什么"，canvas 管"画什么"。**

### 1.1 一个最小应用

写应用只需实现 `app_init()`——主循环、窗口、驱动都由 `demo-main/main_linux.c` 提供（examples 同此结构）：

```c
/* 最小应用: 编译后即显示 标签+按钮+动画画布 */
#include "fxtk.h"
#include <stdio.h>

static void on_btn(fx_widget_t *w, void *ud) {
    static int n = 0;
    char b[32]; snprintf(b, sizeof(b), "被点 %d 次", ++n);
    fx_set_title(w, b);                  /* 改标题, 自动重绘 */
}

static void on_draw(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1);   /* 清底 */
    fx_set_color(FX_YELLOW); fx_draw_circle(cw / 2, ch / 2, ch / 3);
}

void app_init(void) {
    fx_label_new(pixel("10,10", "300,34"), title("第一个应用"));
    fx_button_new(pixel("10,40", "200,80"), title("点我"), call(on_btn));
    fx_canvas_new(pixel("10,90", "470,262"), anim(1), call(on_draw));
}
```

构建运行：`./build_ex.sh <你的文件>`（把文件放进 `examples/`），或参考 `main_linux.c` 自己写 main。

---

## 2. 构建与运行

### 2.1 完整演示

```bash
cd demo-main
./build.sh          # 编译并直接运行 fxtk_sim (完整 12 页演示)
```

### 2.2 独立示例

```bash
./build_ex.sh              # 全构建 examples/ 下所有示例
./build_ex.sh all --no-run # 只全构建, 不逐个运行
./build_ex.sh ex03_anim    # 只构建并运行单个示例
```

### 2.3 环境变量

| 变量 | 作用 |
|---|---|
| `FXTK_FONT=/path/to.ttf` | 指定字体（默认按内置 fallback 列表找） |
| `FXTK_MAXW=1920` | 限制最大逻辑宽度（缩放封顶） |
| `FXTK_STAT=1` | 每秒打印 GPU 顶点数/帧耗时统计 |
| `FXTK_COMPACT=1` | 大屏不做 UI 缩放上限（全比例） |
| `FXTK_BENCH=1` | 关垂直同步，测真实渲染帧率 |

### 2.4 Windows

见 `docs/windows.md`：WSL2+WSLg 零改动 / MSYS2 原生 exe / MSVC+vcpkg。

---

## 3. 坐标系统与布局

### 3.1 设计基线

- 设计基线为 **480×272**（`FX_DESIGN_W/H`）。
- 核心按窗口实际尺寸计算 `s_sx1000 = width*1000/480`、`s_sy1000 = height*1000/272`，所有 `pixel()` 坐标按此**等比缩放**。
- 因此界面在任何分辨率下都**铺满且比例一致**：480×272 的窗口显示原尺寸，1920×1080 全屏自动放大 4 倍。

### 3.2 三种布局模式

| 模式 | 属性宏 | 说明 |
|---|---|---|
| 像素 | `pixel("x1,y1","x2,y2")` | 480×272 设计坐标，等比缩放。**最常用** |
| 百分比 | `percent("a,b","c,d")` | 0.0~1.0 浮点字符串，相对父控件矩形 |
| 网格 | `grid("名字",r1,c1,r2,c2)` | 挂在命名 grid 的格子区间；`grid("名字")` 铺满 |

```c
/* 像素: 固定位置 */
fx_label_new(pixel("6,36", "240,56"), title("左上角"));
/* 百分比: 铺满父容器 */
fx_canvas_new(percent("0,0", "1,1"), name("full"));
/* 网格: 键盘按键 */
fx_grid_map(pixel("6,32", "280,220"), line(3), row(3), name("keys"));
fx_button_new(grid("keys", 2, 2, 2, 2), title("5"), call(on_key));
```

### 3.3 缩放上限（大屏保护）

默认控件最大放大 1.6 倍（`fx_set_max_scale(1.6f)`），大屏不至于控件巨大化。
自绘内容如需跟随 UI 缩放：

```c
int rh = 20 * fxtk_ui_scale() / 100;   /* fxtk_ui_scale(): 480 宽=100 */
if (rh < 12) rh = 12;
```

---

## 4. 颜色系统

颜色为 **24bit RGB**（`uint32_t`，格式 `0xRRGGBB`），与主流标准一致。

### 4.1 构造与常量

```c
fx_set_color(FX_RGB(33, 150, 243));     /* 任意 8bit 分量 */
```

| 常量 | 值 | 常量 | 值 |
|---|---|---|---|
| `FX_BLACK` | `0x000000` | `FX_WHITE` | `0xFFFFFF` |
| `FX_RED` | `0xFF0000` | `FX_GREEN` | `0x00FF00` |
| `FX_BLUE` | `0x0000FF` | `FX_YELLOW` | `0xFFFF00` |
| `FX_CYAN` | `0x00FFFF` | `FX_MAGENTA` | `0xFF00FF` |
| `FX_GRAY` | `0x848484` | `FX_LGRAY` | `0xC8C8C8` |

### 4.2 默认配色（重点！）

**控件不指定颜色也能直接看**，默认即"浅底深字"：

| 控件 | 默认背景 | 默认前景 |
|---|---|---|
| 窗口 | `FX_RGB(245,245,245)` | — |
| label / checkbox | 透明（窗口底色） | `FX_RGB(40,40,40)` 深字 |
| 按钮 | `FX_RGB(33,150,243)` 蓝 | `FX_WHITE` 白字 |
| grid / panel / tab | `FX_RGB(240,240,240)` | `FX_LGRAY` 网格线 |
| slider / progress | `FX_RGB(76,175,80)` 绿 | `FX_LGRAY` 轨道 |
| canvas | `FX_RGB(240,240,240)` | — |
| textedit | 白底 | 黑字 |

所以：

```c
/* 不需要任何 color()/fgcolor() 就能好看 */
fx_label_new(pixel("10,10","300,34"), title("默认深字浅底"));
fx_button_new(pixel("10,40","200,80"), title("默认蓝底白字"));
fx_grid_map(pixel("10,80","300,220"), line(3), row(3));   /* 浅底浅网格线 */
```

需要改色时再显式指定：

```c
fx_button_new(pixel("10,40","200,80"), title("红色"),
              color(FX_RGB(244,67,54)));                /* 红底白字 */
fx_label_new(pixel("10,10","300,34"), title("灰注释"),
             fgcolor(FX_RGB(120,120,120)));
```

改窗口背景：`fx_set_bg(FX_RGB(245,245,245))`；读当前背景：`fx_get_bg()`。

---

## 5. 控件创建（属性宏）

所有 `_new` 均为**变参属性列表**风格：属性用宏构造，顺序任意。

```c
fx_widget_t *fx_label_new(pixel("10,10","300,34"), page(3),
                          line(14), row(1), title("你好"),
                          fgcolor(FX_RGB(51,51,51)), call(on_x));
```

### 5.1 通用属性宏（全控件可用）

| 宏 | 作用 |
|---|---|
| `pixel(a,b)` / `percent(a,b)` / `grid(...)` | 布局矩形（见第 3 章） |
| `title("...")` | 标题/文本 |
| `name("...")` | 控件名（供 `fx_find` 查找，**必须唯一**） |
| `call(fn)` | 回调 `void fn(fx_widget_t *w, void *ud)` |
| `color(c)` | 背景色 |
| `fgcolor(c)` | 前景色（文字/轨道色） |
| `line(n)` | 字号（label 默认 18；按钮/label 均可） |
| `row(n)` | 对齐：0 左 / 1 中 / 2 右 |
| `value(n)` | 初始值 0~100（slider/progress/checkbox 等） |
| `page(n)` | 挂到 tab 的第 n 页 |
| `anim(1)` | canvas 每帧重绘（动画） |
| `border(n)` / `radius(n)` | 边框宽度 / 圆角半径 |
| `dense()` | grid 密集模式（格子无内边距） |

### 5.2 回调与用户数据

```c
static void on_key(fx_widget_t *w, void *ud) {
    int id = (int)(intptr_t)ud;          /* 从 ud 取参数 */
    ...
}
/* 创建时带参数 */
fx_set_cb(w, on_key, (void *)(intptr_t)3);     /* 或创建后设置 */
```

---

## 6. 控件类型详解

### 6.1 `fx_label_new(...)` — 文本标签

`line()` 设字号，`row()` 设对齐（0/1/2），`fgcolor()` 设文字色。

```c
fx_label_new(pixel("8,30","472,50"), line(15), row(1),
             title("居中标题"), fgcolor(FX_RGB(40,40,40)));
```

运行时：`fx_set_title(w,"新文本")`、`fx_set_fontsize(w,20)`、`fx_set_align(w,1)`。

### 6.2 `fx_button_new(...)` — 按钮

`call()` 在**点击释放**时触发。自带按压态、高光/阴影立体效果。

```c
fx_button_new(pixel("340,40","400,70"), name("br_r"), page(6),
              title("红"), color(FX_RGB(244,67,54)), call(on_brush));
```

### 6.3 `fx_canvas_new(...)` — 画布（最灵活）

- `call()` 为**绘制回调**（立即式）。
- `anim(1)`：每帧重绘（动画）；否则仅脏区/首次绘制。
- 回调里坐标是**控件本地坐标**（0,0 为左上角），宽高用 `fx_canvas_size(w,&cw,&ch)` 取（也可 `fx_widget_rect`）。
- 清底用 `fx_canvas_clear(w,color)`（替代手写 `fx_set_color`+`fx_fill_rect`）。
- canvas 可以当**容器**：`fx_parent(canvas)` 后创建的子控件会画在画布内容之上（C2 补画），随画布裁剪。

```c
fx_canvas_new(pixel("6,32","444,236"), name("wave_cv"), page(0),
              anim(1), color(FX_RGB(30,30,30)), call(on_wave));
```

### 6.4 `fx_slider_new(...)` — 滑杆

`value()` 初始值；`call()` 在拖动时持续回调，`fx_get_value(w)` 取当前值 0~100。

```c
static void on_speed(fx_widget_t *w, void *ud) {
    int v = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), v);    /* 进度条联动 */
}
fx_slider_new(pixel("60,194","292,212"), name("speed"), value(30), call(on_speed));
```

### 6.5 `fx_progress_new(...)` — 进度条

只读展示，`value()` 0~100，`fx_set_value` 更新。

```c
fx_progress_new(pixel("300,196","444,210"), name("speed_bar"), value(30));
```

### 6.6 `fx_checkbox_new(...)` — 复选框

`title()` 为右侧文字；点击翻转 `value`（0/1）；`call()` 回调。

```c
fx_checkbox_new(pixel("6,218","150,240"), name("wave"), title("波形开关"),
                value(1), call(on_wave));
```

### 6.7 `fx_textedit_new(...)` — 输入框（桌面扩展，需 `fxtk_desktop.h`）

- 键盘输入、Backspace、方向键、Home/End。
- **框选**：Shift+方向键 / Shift+点击拖动。
- **系统剪贴板**：Ctrl+A 全选、Ctrl+C 复制、Ctrl+V 粘贴、Ctrl+X 剪切。
- `maxlen(n)`：限长并显示 `n/max` 计数。
- 滚轮在超长内容时滚动。右键输入框弹置顶菜单（复制/粘贴/全选）。

```c
fx_textedit_new(pixel("6,60","444,140"), name("edit1"), title("hello 你好"));
fx_textedit_new(pixel("6,148","444,182"), name("edit2"), title(""), maxlen(20));
```

### 6.8 `fx_list_new_p("r1","r2",pg)` — 列表

- `pg` 控件归属的**标签页页码**（`-1` 表示不属于任何页签）；滚轮滚动；点击选中高亮。
- 数据：`fx_list_add(w, text)`；选中回调：`fx_list_set_cb(w, fn)`。
- 回调里 `fx_list_sel(w)` 取选中行号。

```c
fx_widget_t *list = fx_list_new_p("8,106","200,236", 3);   /* 放进第 3 个页签 */
for (int i = 0; i < 12; i++) fx_list_add(list, names[i]);
fx_list_set_cb(list, on_nc_pick);
```

### 6.9 `fx_drop_new_p("r1","r2",pg)` — 下拉框

- `pg` 控件归属的**标签页页码**（`-1` 表示不属于任何页签）。
- `fx_drop_add` 加选项；`fx_list_set_cb` 设选中回调。
- 靠底部**自动向上弹**；弹层内容超出带滚动，不越界；点外部自动收起。

```c
fx_widget_t *drop = fx_drop_new_p("220,106","360,132", 3);
fx_drop_add(drop, "字体: 小");
fx_drop_add(drop, "字体: 中");
fx_drop_add(drop, "字体: 大");
fx_list_set_cb(drop, on_nc_drop);
```

### 6.10 其他

| 控件 | 说明 |
|---|---|
| `fx_panel_new(...)` | 面板容器（浅底 + 深边框） |
| `fx_tab_new(...)` | 标签页容器（见第 7 章） |
| `fx_grid_map(...)` | 网格容器（背景 + 网格线） |
| `fx_image_new(...)` | 图片控件（见第 11 章） |

---

## 7. 容器与页面

### 7.1 父子关系

创建控件时挂到"当前父控件"下：

```c
fx_parent(fx_find("tab"));       /* 之后创建的控件挂到 tab 下 */
... 创建一批控件 ...
fx_parent(NULL);                 /* 回到根 */
```

### 7.2 标签页（tab）与页面

```c
fx_tab_new(pixel("10,26","470,266"), title("波形,图形,控件,图片"),
           name("tab"), color(FX_RGB(224,224,224)));
fx_parent(fx_find("tab"));
fx_canvas_new(pixel("6,32","444,196"), name("wave_cv"), page(0), anim(1), call(on_wave));
fx_button_new(pixel("6,202","76,236"), page(1), title("切换模式"), call(on_mode));
fx_parent(NULL);
```

- `page(n)` 决定控件在第 n 页显示，`tab` 的 `value`（当前页）由点击页签切换。
- 页签选中态有**立体凸起**（亮底深字 + 高光），未选中下凹。
- tab 标题用逗号分隔页签名。

### 7.3 canvas 当容器

```c
fx_canvas_new(pixel("6,32","444,196"), name("gfx_cv"), page(1), anim(1), call(on_gfx));
fx_parent(fx_find("gfx_cv"));     /* 子控件画在画布内容之上 */
fx_button_new(pixel("20,40","90,64"), page(1), title("内嵌"), call(on_mode));
fx_parent(fx_find("tab"));        /* 恢复父级 */
```

---

## 8. 绘制原语（canvas 立即式绘制）

坐标均为**控件本地坐标**（0,0 为控件左上）。

### 8.1 基础形状

| 函数 | 说明 |
|---|---|
| `fx_set_color(c)` | 设当前绘制色 |
| `fx_draw_pixel(x,y)` | 单像素 |
| `fx_fill_rect(x1,y1,x2,y2)` | 实心矩形 |
| `fx_draw_rect(x1,y1,x2,y2)` | 矩形边框 |
| `fx_draw_hline(x1,x2,y)` | 水平线 |
| `fx_draw_vline(x,y1,y2)` | 垂直线 |
| `fx_draw_line(x1,y1,x2,y2)` | 任意直线 |
| `fx_draw_circle(cx,cy,r)` / `fx_fill_circle` | 圆 |
| `fx_draw_ellipse(cx,cy,rx,ry)` / `fx_fill_ellipse` | 椭圆 |
| `fx_draw_arc(cx,cy,r,a1,a2)` / `fx_fill_arc` | 圆弧（角度制） |
| `fx_draw_rect_round(...)` / `fx_fill_rect_round(...)` | 圆角矩形 |
| `fx_fill_rect_gradient(x1,y1,x2,y2,c1,c2,vertical)` | 渐变填充：`c1→c2` 线性过渡（`vertical=1` 上下，`0` 左右） |

```c
fx_fill_rect_gradient(20, 20, 240, 120, FX_BTN_BLUE, FX_OK_GREEN, 1);   /* 蓝→绿 上下渐变 */
fx_fill_rect_gradient(20, 140, 240, 170, FX_RED_ACCENT, FX_YELLOW, 0);  /* 红→黄 左右渐变 */
```

> 注：渐变/抗锯齿都需要能**读回目标像素**，因此作用于**离屏/帧缓冲**路径（抗锯齿默认开启时画布自动离屏）。

### 8.1a 抗锯齿（v2.2，默认开启）

`fx_set_aa(1)` 开启抗锯齿（**v2.2 默认开**，`FX_AA_DEFAULT=0` 可默认关，`fx_set_aa(0)` 运行时关）。
`line/circle/fill_circle/rect/fill_rect_round/arc/ellipse/fill_ellipse` 会按"到图形的距离/子像素覆盖"做边缘混合，边缘平滑无锯齿。
**开启后画布自动离屏**以支持混合（无需手动 `fx_canvas_set_buf`）。

```c
fx_set_aa(1);               /* 让后续画布自动离屏 + 抗锯齿 */
fx_canvas_new(pixel("6,32","444,236"), anim(1), call(on_draw));
...
fx_set_aa(0);               /* 关闭(恢复硬边/直接绘制) */
```

### 8.1b 画布便捷（v2.2）

```c
int cw, ch; fx_canvas_size(w, &cw, &ch);    /* 取画布本地宽高 (替代重复的 fx_widget_rect+cw/ch) */
fx_canvas_clear(w, FX_WINDOW_BG);           /* 一键清底到颜色 */
```

### 8.2 多边形

```c
int16_t tri[6] = { 0, -20, 17, 10, -17, 10 };
fx_set_color(FX_YELLOW);
fx_fill_polygon(tri, 3);      /* 三角形 (点数组 x,y 交错) */
fx_draw_polygon(tri, 3);      /* 描边 */
```

### 8.3 文字

```c
/* 基础字号 (默认 18) */
fx_draw_text_c(10, 12, "你好", FX_WHITE, FX_RGB(30,30,30));
/* 指定字号 + 测宽居中 */
int tw = fxtk_text_width_size(16, "居中");
fxtk_draw_text_size(16, (cw - tw) / 2, 10, "居中", FX_WHITE, FX_BLACK);
```

### 8.4 裁剪

```c
fx_set_clip(x1, y1, x2, y2);      /* 之后绘制限在此区域 */
fx_reset_clip();                  /* 恢复全画布 */
```

### 8.5 典型画布回调模板

```c
static void on_view(fx_widget_t *w, void *ud) {
    int cw, ch; fx_canvas_size(w, &cw, &ch);        /* v2.2: 取画布本地宽高 */
    fx_canvas_clear(w, FX_WINDOW_BG);               /* v2.2: 一键清底 */

    fx_set_color(FX_RGB(33,150,243));
    fx_fill_rect(10, 10, cw - 10, 40);              /* 内容 */
    fx_set_color(FX_RGB(40,40,40));
    fx_draw_text_c(20, 18, "状态: OK", FX_WHITE, FX_RGB(33,150,243));
}
```

> 早期写法（仍可用）是 `fx_widget_rect` + 手动 `fx_set_color`+`fx_fill_rect` 清底；v2.2 的
> `fx_canvas_size` / `fx_canvas_clear` 专门替代这两段样板，画布代码更短。

---

## 9. 输入：触摸/键盘/滚轮

> 本节 API（`fx_touch_state`/`fx_last_key`/`fx_pressed`/`fx_wheel_take`/`fx_set_focus`/`fx_get_focus` 等）属于**桌面扩展**，均在 `fxtk_desktop.h` 声明，使用前需 `#include "fxtk_desktop.h"`（文本框等桌面控件同理）。

### 9.1 鼠标/触摸状态

```c
int mx, my, mp;
fx_touch_state(&mx, &my, &mp);     /* 坐标 + 是否按下; 悬停也实时更新 */
```

- 坐标是**窗口逻辑坐标**（缩放后，与控件 x1..y2 同一坐标系）。
- `fx_pressed()`：返回当前按下的控件指针（非 NULL 即按下中），可用于自绘按压态或拖动判定。

```c
static void on_canvas(fx_widget_t *w, void *ud) {
    int mx, my, mp; fx_touch_state(&mx, &my, &mp);
    if (mp && fx_pressed() == w) { ... /* 正在画布上按住 */ }
}
```

### 9.2 键盘

```c
fx_keyev_t k = fx_last_key();          /* 最近一次按键 */
if (k.utf8[0])      ...  /* 可打印字符 (UTF-8) */
else if (k.key == FX_KEY_UP)    ...  /* 方向键 */
else if (k.key == FX_KEY_RETURN) ...  /* 回车 */
```

功能键枚举：`FX_KEY_BACKSPACE/RETURN/ESCAPE/LEFT/RIGHT/HOME/END/UP/DOWN/DELETE`。

### 9.3 滚轮与核心滚动

驱动累积滚轮像素增量（`fx_poll` 自动路由到光标下的控件）。**推荐自绘滚动直接用一行库 API**：

```c
int off = fx_scroll_update(w, total);   /* 核心滚动: 滚轮/插值/重绘全在库内, 返回当前偏移 */
fx_scrollbar_draw(w, off, total);       /* 库滚动条: 轨道+滑块, 支持鼠标拖拽 */
```

要点：
- **手感**：目标像素累积 + 每帧 25% 插值（rc 版手感），滚轮停后快速收尾，无惯性滞留；
- **零重绘**：偏移不变时不请求重绘；
- **多控件**：内部 8 槽状态池，多个画布可并行滚动；
- **拖拽**：滚动条滑块支持鼠标拖动（含自绘画布，拖动时同步状态池）；
- 底层原语 `int dy = fx_wheel_take(w);` 仍可用，需要完全自控时再手动处理。

---

## 10. 运行时操作控件

| 函数 | 说明 |
|---|---|
| `fx_find("name")` | 按唯一名字查控件 |
| `fx_set_title(w, s)` | 改标题并重绘 |
| `fx_set_value(w, v)` | 改值（slider/progress/checkbox）0~100 |
| `fx_get_value(w)` | 读值 |
| `fx_set_color_w(w, c)` | 改背景色并重绘 |
| `fx_set_fgcolor(w, c)` | 改前景色 |
| `fx_set_cb(w, fn, ud)` | 替换回调 + 用户数据 |
| `fx_set_visible(w, v)` | 显隐 |
| `fx_widget_rect(w, &x1,&y1,&x2,&y2)` | 取当前矩形（屏幕坐标） |
| `fx_widget_set_rect(w, x1,y1,x2,y2)` | 移动/缩放控件（自动重绘新旧区域） |
| `fx_widget_type(w)` / `fx_widget_title(w)` | 查询类型 / 标题 |
| `fx_set_fontsize(w, n)` / `fx_set_align(w, a)` | 字号 / 对齐 |

### 10.1 移动控件（动画/压测）

```c
int bx = x1 + 20 + (int)((cw - 170) * (0.5f + 0.5f * sinf(t * 0.023f)));
fx_widget_set_rect(b, bx, by, bx + 120, by + 36);
```

### 10.2 运行时创建控件（动态加控件）

```c
static fx_widget_t *s_dyn[4000];      /* 指针缓存 */
if (fxtk_fps() >= 30 && n < cap) {
    fx_parent(fx_find("move_cv"));
    fx_widget_t *nb = fx_button_new(pixel("0,0","0,0"), title("动态"),
                                    color(FX_RGB(156,39,176)), call(on_click));
    fx_parent(fx_find("tab"));
    fx_widget_fix(nb, x, y);          /* 固定坐标: 防布局重算复位 */
    fx_widget_set_rect(nb, x, y, x+20, y+16);
    s_dyn[n++] = nb;
}
```

- `fx_widget_fix(w, x, y)`：切到**固定坐标模式**（`FX_POS_FIXED`），后续 `fx_layout()`（resize/新建控件触发）不会用 ox/oy 复位它，同时记录移动基准点。
- 控件池默认 **4096** 槽（`FX_MAX_WIDGETS`），`fxtk_widget_count()` 查当前存活数。
- 动态控件建议用指针数组缓存，避免每帧 `fx_find` 线性扫描。

### 10.3 删除控件

```c
fx_widget_t *w = fx_find("tmp");
if (w) fx_delete(fx_wptr(w));         /* 用 fx_wptr(控件指针) 或 grid(name) 定位删除 */
```

---

## 11. 图片与特效

### 11.1 创建/加载图片（`fxtk_image.h` + SDL_image）

```c
fx_image_t *img = fx_image_create(w, h);      /* 空白图, 可离屏渲染 */
fx_image_set_px(img, x, y, color);            /* 逐像素填充 */
fx_image_free(img);                           /* 释放 */

/* 自绘生成程序贴图 */
static fx_image_t *make_pic(int kind) {
    fx_image_t *im = fx_image_create(120, 90);
    for (int y = 0; y < 90; y++)
        for (int x = 0; x < 120; x++)
            fx_image_set_px(im, x, y, FX_RGB(x * 255 / 119, y * 255 / 89, 140));
    return im;
}
```

> PC 端还有 `fx_image_load(path)`（SDL_image 解码，见 demo-main/fxtk_image_sdl.c）。

### 11.2 图片控件

```c
fx_image_new(pixel("6,32","280,220"), name("pic"), image(img), call(on_img));
fx_set_image(w, new_img);             /* 换图 */
fx_image_set_zoom(w, 150);            /* 缩放 10~400% */
```

### 11.3 特效（`fxtk_effects.h`，CPU 像素级）

| 函数 | 说明 |
|---|---|
| `fx_draw_image(img,x,y,dw,dh)` | 缩放绘制到画布 |
| `fx_draw_image_ex(img,x,y,dw,dh,dark)` | 同上，dark=按压缩暗 |
| `fx_draw_image_rot(img,cx,cy,deg,pct)` | 旋转 + 缩放贴图（GPU 硬件旋转） |
| `fx_image_flip_x/y(img)` | 水平镜像 / 垂直翻转 |
| `fx_image_grayscale(img)` | 灰度 |
| `fx_image_tint(img,c,amount)` | 染色 0~255 |
| `fx_image_brightness(img,delta)` | 亮度 -255~255 |

```c
fx_draw_image_rot(s_pics[0], cw/4, ch/2, ang, 90);   /* 旋转贴图 */
fx_image_grayscale(pic);                             /* 先灰度 */
```

---

## 12. 文本与字体

- 基础字号 18（`fxtk_font_init` 设定）。
- `fxtk_draw_text_size(size,...)`：任意字号，内部**字体缓存**（8 个 TTF，满则淘汰关闭）。
- 文本按 (字体, 文本, fg, bg) 缓存为**纹理**，LRU 淘汰——重复文本零 CPU 开销。
- 中文字体：内置 fallback（wqy-zenhei / Noto Sans CJK / msyh），`FXTK_FONT` 可覆盖。

```c
fxtk_draw_text_size(16, x, y, "大字号", FX_WHITE, FX_BLACK);
int w = fxtk_text_width_size(16, "大字号");   /* 测宽 */
```

---

## 13. 弹层与右键菜单

- **右键菜单**：在输入框上右键弹出置顶菜单（复制/粘贴/全选），`fxtk_right_click` 由驱动回报。
- **下拉弹层**：`fx_drop` 展开的选项层为顶层弹层，绘制在所有控件之上，不被页面裁剪。
- 靠底部自动向上弹；内容超出带滚动；点击外部自动关闭。

---

## 14. 重绘与性能模型

### 14.1 重绘机制

| 触发 | 行为 |
|---|---|
| `fx_repaint()` | 全量重绘 |
| `fx_repaint_rect(x1,y1,x2,y2)` | 脏区重绘（当前实现提升为全量，保证无残影） |
| 控件属性改动（`fx_set_title` 等） | 自动局部重绘 |
| `anim(1)` 画布 | **每帧**回调 + 重绘 |

### 14.2 渲染管线（GPU）

- 控件绘制 → SDL 顶点批（矩形/三角/折线一次提交）→ 文本纹理 blit → present。
- 图像缩放/旋转走 GPU 纹理变换，CPU 只算 24bit RGB 像素。
- `FXTK_STAT=1` 打印顶点数与帧耗时，用于性能调优。

### 14.3 性能建议

1. **静态内容**不要 `anim(1)`；变化时手动 `fx_repaint_rect`。
2. **动画内容**用 `anim(1)` canvas，内部避免每帧 `fx_find`（缓存指针）。
3. **大量粒子/图元**：画到小离屏图再 `fx_draw_image` 放大（GPU blit），如粒子页 2x 缩小缓冲。
4. **动态加控件**：控件池 4096 上限，帧率富余（≥30fps）再加，防止失控。
5. **文字**：相同文本/颜色会自动走纹理缓存，避免拼接易变字符串为键。

---

## 15. 完整示例：从零写一个波形页

目标：一页"实时波形 + 速度滑杆 + 开关"，完全体现 label/button/slider/checkbox/canvas 协作。

```c
#include "fxtk.h"
#include <math.h>

static int s_phase = 0, s_speed = 30, s_on = 1;

/* 画布回调: 每帧画波形 */
static void on_wave(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;

    fx_set_color(FX_RGB(30, 30, 30));
    fx_fill_rect(0, 0, cw - 1, ch - 1);          /* 清底 */

    if (!s_on) return;                            /* 开关关闭不画 */

    int amp = ch / 2 - 8, prev_y = ch / 2;
    fx_set_color(FX_YELLOW);
    for (int x = 0; x < cw; x++) {                /* 双频叠加波形 */
        float t = (float)(x + s_phase) * 0.1f;
        int y = ch / 2 + (int)(amp * (0.7f * sinf(t) + 0.3f * sinf(t / 3.0f)));
        fx_draw_line(x - 1, prev_y, x, y);
        prev_y = y;
    }
    s_phase += s_speed;                           /* 相位推进 */
    if (s_phase > 4096) s_phase -= 4096;
}

/* 滑杆回调 */
static void on_speed(fx_widget_t *w, void *ud) {
    (void)ud;
    s_speed = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), s_speed);  /* 进度条联动 */
}

/* 开关回调 */
static void on_wave_sw(fx_widget_t *w, void *ud) {
    (void)ud;
    s_on = fx_get_value(w) != 0;
}

void build_wave_page(void) {
    fx_tab_new(pixel("10,26", "470,266"), title("波形,图形,控件"),
               name("tab"), color(FX_RGB(224,224,224)));
    fx_parent(fx_find("tab"));

    fx_canvas_new(pixel("6,32", "444,188"), name("wave_cv"), page(0),
                  anim(1), color(FX_RGB(30,30,30)), call(on_wave));
    fx_label_new(pixel("6,194", "56,212"), page(0), title("速度"));
    fx_slider_new(pixel("60,194", "292,212"), name("speed"),
                  page(0), value(30), color(FX_RGB(76,175,80)), call(on_speed));
    fx_progress_new(pixel("300,196", "444,210"), name("speed_bar"),
                    page(0), value(30));
    fx_checkbox_new(pixel("6,218", "150,240"), name("wave"),
                    page(0), title("波形开关"), value(1), call(on_wave_sw));

    fx_parent(NULL);
}

/* 入口只需 app_init() (main_linux.c 提供主循环) */
void app_init(void) { build_wave_page(); }
```

---

## 16. 常见坑与最佳实践

### 常见坑

1. **回调里坐标是本地坐标**：从 0 开始，不要加控件屏幕位置。控件位置用 `fx_widget_rect` 拿。
2. **`anim(1)` 才每帧画**：静态画布改内容后要手动 `fx_repaint_rect`。
3. **`page(n)` 必须配合 tab**：否则控件不随页切换显隐（直接显示在根上）。
4. **改矩形用 `fx_widget_set_rect`**：直接改 `w->x1`（结构体不透明）不会触发重绘。
5. **控件名全局唯一**：`fx_find` 线性扫描全池，重复名返回第一个。
6. **动态创建控件会被布局复位**：创建后必须 `fx_widget_fix(w,x,y)` 固定坐标，否则下一次 `fx_layout()` 会把它拉回设计坐标原点。
7. **每帧 `fx_find` 是 O(4096)**：热点循环里用指针缓存（如压测页 `s_dyn[]`）。
8. **字号用 `line()`/`fx_set_fontsize`**：自绘文字用 `fxtk_draw_text_size`，不要手写缩放字号。
9. **颜色是 24bit RGB**：`0xRRGGBB`，直接用 `FX_RGB(r,g,b)` 或十六进制字面量（如 `0xFF0000` 红）。
10. **窗口关闭/退出**：SDL 收到 QUIT 事件自动 `exit(0)`，无需处理。

### 最佳实践清单

- 静态 UI 全部用默认配色，仅强调元素显式 `color()`。
- 布局优先 `pixel()` 设计坐标；铺满用 `percent()`；表格用 `grid()`。
- 控件要可交互就 `name()` 命名；纯展示可匿名。
- 动画频率不要超过 60fps；`anim(1)` 画布内先 `fill_rect` 清底再画。
- 离屏图（`fx_image_create`）用完 `fx_image_free`。
- 大列表/滚动优先"画布自绘 + 可见行裁剪"（参考 `on_scroll_view`），不要堆上千子控件。

---

## 附录：驱动与移植

fxtk 核心通过 `fx_driver_t` 抽象平台：

```c
typedef struct {
    uint16_t width, height;
    int  (*init)(void);
    void (*set_window)(x0,y0,x1,y1);      /* 像素流窗口 */
    void (*push_pixels)(px, n);           /* 像素推送 */
    void (*fill_rect)(x0,y0,x1,y1,color); /* 矩形(驱动加速) */
    int  (*touch_read)(x,y,pressed);
    int  (*key_read)(fx_keyev_t*);
    void (*blit_img)(...);                /* 图像缩放 blit */
    void (*fill_tri)(...);                /* GPU 三角形 */
    void (*draw_line)(...);               /* GPU 折线 */
    void (*blit_tex)(...);                /* 文本纹理 blit */
    void (*blit_img_rot)(...);            /* GPU 旋转 blit */
    ...
} fx_driver_t;
```

PC 端实现见 `demo-main/fxtk_sdl_driver.c`；换平台只需实现该结构体并 `fx_init(&drv)`。

## 附录：主题、滚动容器与 GPU 光追

### 主题（浅/深两套配色）

颜色用 `fx_colorx_t { light, dark }` 提供两套值，当前主题自动选一套：

```c
fx_set_global_background((fx_colorx_t){ FX_RGB(245,245,245), FX_RGB(30,30,34) });
fx_set_dark_theme(1);                /* 1=深色, 0=浅色 */
int dark = fx_is_dark_theme();
fx_color_t c = fx_colorx_current(some_colorx);   /* 按当前主题取值 */
```

（`fx_set_bg` 只改浅色背景；调用 `fx_set_dark_theme` 时控件会统一重刷。）

### 滚动容器

`fx_scroll_new(...)` 创建一个可滚动容器，内容高度超出后出现滚动条：

```c
fx_widget_t *sc = fx_scroll_new(pixel("0,0","480,200"));
fx_parent(sc);                       /* 后续控件挂进滚动容器 */
fx_button_new(pixel("10,10","200,50"), title("第 1 项"), call(...));
...
fx_scroll_content(sc, 1200);         /* 内容总高(像素), 超出才可滚 */
```

需要更细控制时用平滑滚动：`int off = fx_scroll_update(w, content_h)`（目标像素 + 25% 逐帧插值），配合 `fx_scrollbar_draw(w, off, content_h)` 画滚动条。

### GPU 光追（3D 页）

demo 的 3D 页用 Shadertoy 风格软件光线步进（`raymarch_render`）打到离屏缓冲，可选 GPU(GLSL) 通道 `gpu_raymarch_render`：

```c
raymarch_render(px, w, h, time, max_steps);   /* CPU 软光追, 24bit RGB */
gpu_raymarch_start();                          /* 启动 GPU 线程 */
if (gpu_raymarch_ok()) gpu_raymarch_render(px, w, h, time);   /* GPU 优先 */
const char *r = gpu_raymarch_renderer();       /* 返回渲染器名 */
```

`gpu_raymarch_ok()` 返回 0 时自动退回 CPU（无硬件 GL 的 VM/CI 环境）；Windows 下由 `gpu_stub_win.c` 提供空实现。场景含解析地面/球 + 步进圆环 + 软阴影 + 雾。

> 若需了解控件绘制、布局、重绘的源码细节，见 `docs/internals.md`。
