# fxtk 速查卡（给上下文/速度受限的小模型）

**只读这一页就够写程序。** 完整说明与陷阱表 → `REFERENCE.md`。
想看能跑的计算器 → `calc.c`。

## 骨架（照抄，别改结构）

```c
#include "fxtk.h"
#include "fxtk_desktop.h"   /* 文本框/滚动/ maxlen 在这里，不在 fxtk.h */
#include "fxtk_app.h"       /* 它提供 main()，你不要写 main */
#include <stdio.h>

void fxtk_app_init(void)    /* 唯一必需函数 */
{
    fx_set_bg(FX_RGB(240,240,244));
    /* 在这里建控件 */
}
```

## 建控件（一行一个）

```c
fx_label_new(  pixel("10,8","200,30"), title("文字"));
fx_button_new( pixel("10,40","100,74"), title("按钮"), call(on_click));
fx_checkbox_new(pixel("10,80","100,110"), title("选"), call(on_check));
fx_slider_new( pixel("10,120","200,136"), value(50));
fx_progress_new(pixel("10,140","200,154"), value(30));
fx_canvas_new( pixel("10,160","200,260"), anim(1), call(on_draw));
```

**位置永远是 `pixel("x1,y1","x2,y2")` —— 两个字符串，不是四个数字。**
坐标基于 480×272 设计画布，窗口缩放自动适配。

## 回调

```c
static void on_click(fx_widget_t *w, void *ud)
{
    (void)ud;
    const char *t = fx_widget_title(w);   /* 按钮上的文字 */
    fx_set_title(w, "改文字");             /* 改完自动重绘，不要调 fx_repaint */
}
```

## 改控件 / 取值

```c
fx_widget_t *d = fx_find("disp");    /* 按 name() 找 */
fx_set_title(d, "新文字");
fx_get_value(w);                      /* 复选框/滑条 0..100 */
fx_textedit_text(d);                  /* 文本框内容 (const char*) */
```

## 文本框

```c
fx_textedit_new(pixel("10,8","470,58"), name("disp"), title("0"));
fx_textedit_set_readonly(d, 1);      /* 只读: v2.4.5+ 才有 */
```

## 画布（本地坐标，(0,0)=画布左上角）

```c
static void on_draw(fx_widget_t *w, void *ud)
{
    (void)ud; int cw, ch;
    fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(255,255,255));
    fx_set_color(FX_RGB(33,150,243));
    fx_fill_rect(10, 10, 100, 60);
    fx_draw_circle(cw/2, ch/2, 20);
    fx_draw_text(6, 6, "hi");
}
```

绘图函数：`fx_fill_rect fx_draw_rect fx_fill_circle fx_draw_circle fx_draw_line fx_draw_text`

## 颜色

`FX_RGB(r,g,b)` 或 `FX_WHITE FX_BLACK FX_RED FX_GREEN FX_BLUE FX_YELLOW FX_GRAY FX_LGRAY`

## 编译

```bash
cd your_app && make && ./your_app     # 把代码写进 your_app/main.c
```

## 三个最容易犯的错

1. **写了自己的 `main()`** → 不要写，外壳提供。入口是 `fxtk_app_init`。
2. **`pixel(10,10,100,40)`** → 错。要 `pixel("10,10","100,40")`。
3. **用了 `fx_set_text` / `fx_rect` / `fx_create_button`** → 都不存在。用 `fx_set_title` / `pixel()` / `fx_button_new`。
