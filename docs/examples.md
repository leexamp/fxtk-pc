# Examples 导读

| 示例 | 看点 |
|---|---|
| ex01_hello | 按钮/标签/复选框、`fx_set_title/fx_set_bg`、深浅色切换 |
| ex02_widgets | `grid()` 键盘、滑条-进度条联动、`fx_set_color_w` 改控件色 |
| ex03_anim | `anim(1)` 画布、波形 + `fx_fill_polygon_rot` 旋转矢量 |
| ex04_edit | 输入框：框选/剪贴板/`maxlen`/滚轮、右键置顶菜单 |
| ex05_scroll | 画布自绘长列表 + `fx_scroll_update` 核心滚动 + `fx_scrollbar_draw`（滑块可拖拽） |
| ex06_img | `fx_image_create` + `fx_draw_image_rot` 旋转贴图 |
| ex07_snake | `anim(1)` 主循环 + `fx_last_key` 方向键 + 数组地图/碰撞/重开 |
| ex08_array_buttons | 循环批量建按钮 + `name()`/回调来源区分 + 批量操作 |
| ex09_lottery | 随机 + 滚动动画 + `fxtk_draw_text_size` 大字号 |
| ex10_extra | 列表/下拉/多字号三件套 `fx_list_new_p`/`fx_drop_add`/`fx_set_fontsize`，弹层不越界 |
| ex11_percent | `percent()`（0.0~1.0）铺满/对齐，与 `pixel()` 对照 |
| ex12_hover | `fx_touch_state` 悬停实时坐标 + 悬停高亮（未按也更新） |
| ex13_popup | 底部下拉自动向上弹 + 输入框右键置顶菜单（需 `fxtk_desktop.h`） |
| ex14_resize | 窗口拖拽布局跟随、resize 无残影 |
| ex15_textcache | 多字号混排 + 字号滑杆，验证字体缓存淘汰不闪断 |
| ex16_dynamic | 运行时动态创建控件：`fx_widget_fix` 固定坐标 + 指针缓存 + 正弦漂移 |
| ex17_tabs | 立体标签页 + `page()` 页闸：三页不同内容（按钮/动画/矢量） |
| ex18_defaults | 默认配色零配置：全部控件不写 `color()`/`fgcolor()` 也好看 |

## 画布学习目录 `examples/canvas/`（v2.2 新增）

专门学习画布用法的教程系列，从基础到进阶，每个都带详尽注释：

| 示例 | 看点 |
|---|---|
| canvas_01_primitives | 全部绘图图元：线/矩形/圆/椭圆/圆弧/三角/多边形/圆角矩形/文字 |
| canvas_02_immediate | 立即模式 + `anim(1)` 每帧回调：画一个"时钟" |
| canvas_03_offscreen | 随窗口缩放的棋盘格（直接绘制，缩放安全）；并讲解离屏缓冲 `fx_canvas_set_buf` 的适用场合与缩放限制 |
| canvas_04_custom_widget | 用画布自绘一个"自定义控件"：圆表 + 环形进度（可复用） |
| canvas_05_image | 画布内渲染图片：程序生成贴图 + 旋转/缩放/灰度/染色 |
| canvas_06_interact | 画布内的交互：按住拖动小球（`fx_touch_state`/`fx_pressed`） |
| canvas_07_aa | 抗锯齿：`fx_set_aa(1)` 后 line/circle/rect/round-rect/arc 边缘平滑，按钮实时切换 ON/OFF |

> v2.2 新增便捷 API：`fx_canvas_size(w,&cw,&ch)` 取画布本地宽高、`fx_canvas_clear(w,color)` 一键清底。
> 运行：`./build_ex.sh canvas_01_primitives` 或 `make -C demo-main canvas_01_primitives`。

### 抗锯齿（v2.2）

`fx_set_aa(1)` 开启边缘抗锯齿：line/circle/fill_circle/rect/round-rect/arc 等矢量图元按"到图形的距离/子像素覆盖"做边缘混合，边缘更平滑（平滑渲染）。**调 `fx_set_aa(1)` 后画布会自动离屏**以支持混合（无需手动 `fx_canvas_set_buf`），见 `canvas_07_aa`。`fx_set_aa(0)` 关闭。

### 画布内容要跟随窗口缩放（重要）

pixel() 画布会随窗口等比放大/缩小，但**画布内部是本地坐标系**. 如果你在回调里写死像素坐标。
窗口拖大后内容就会挤在一角（不符合预期）。办法是**把坐标/尺寸写成 `cw`/`ch` 的比例**，
本目录示例都这么做：

```c
int cw, ch; fx_canvas_size(w, &cw, &ch);
fx_canvas_clear(w, FX_WINDOW_BG);
int px = (int)(cw * 0.5f);                 /* 横向 50% 处 */
fx_fill_circle(px, (int)(ch * 0.5f), (int)(cw * 0.05f));
```

### 离线查看画布渲染结果（无窗口）

`demo-main/test/render_canvas.c` 用一个假驱动把示例渲染成 PPM 图，无需 SDL/窗口/字体，
方便在不同窗口尺寸下检查是否崩坏/错位：

```bash
gcc -O2 -I. -I../components/fxtk test/render_canvas.c ../examples/canvas/canvas_01_primitives.c \
  ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c \
  ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_effects.c -o /tmp/rc -lm
/tmp/rc 800 480 /tmp/frame.ppm       # 800x480 窗口下的渲染
convert /tmp/frame.ppm /tmp/frame.png # 用 ImageMagick 转 PNG 查看
```

运行：
```bash
./build_ex.sh              # 一次全构建并逐个运行全部示例 (含 examples/canvas/)
./build_ex.sh all --no-run # 只全构建不运行
./build_ex.sh ex03_anim    # 单个示例 (换成任意名字)
./build_ex.sh canvas_01_primitives  # 单个画布教程示例
```

## 备注

- **ex13** 用到输入框构造宏 `fx_textedit_new`，它在 `fxtk_desktop.h`，故该例额外 `#include "fxtk_desktop.h"`。
- **ex16** 展示动态控件三要素：`fx_widget_fix`（防布局复位）、指针数组（免 `fx_find`）、帧率门控（≥30fps 才加）。
- **ex17** 的 canvas 在页 1 用 `anim(1)` 动画、页 2 不带动画（静态绘制仅首次/脏区）。
- **默认配色**：控件不指定颜色即为浅底深字（网格浅底浅线、按钮蓝底白字），ex18 全程零显式颜色。
- **ex12/ex14** 的画布回调里用 `fx_widget_rect` 取本地宽高，坐标均为本地系。

## 从示例到完整演示

`demo-main/app.c` + `app_desktop.c` 是 12 页综合演示（含 3D 光追、GPU 粒子、动态压测），运行 `./build.sh` 查看；文档见 `desktop.md`。
