# Changelog

## v2.2

### 新增
- **画布学习教程** `examples/canvas/`：canvas_01_primitives / 02_immediate / 03_offscreen / 04_custom_widget / 05_image / 06_interact / 07_aa，从图元到交互到抗锯齿逐级讲解，带详尽注释。
  - 所有示例把内容写成 `cw`/`ch` 的**比例坐标**，窗口任意缩放（大/小）内容都跟随画布、不挤角、不越界；并用无头渲染验证了 480x272 / 800x480 / 200x112 都不崩坏。
- **无头渲染工具** `demo-main/test/render_canvas.c`：用假驱动把任意 canvas 示例渲染成 PPM，无需 SDL/窗口/字体即可在不同窗口尺寸下查看/回归（CI 已加入渲染冒烟测试）。
- **便捷画布 API**：
  - `fx_canvas_size(w, &cw, &ch)` — 取画布本地宽高，替代重复的 `fx_widget_rect` + 宽高样板。
  - `fx_canvas_clear(w, color)` — 一键把画布清成指定颜色，替代 `fx_set_color` + `fx_fill_rect` 样板。
- **抗锯齿（平滑渲染，默认关闭/opt-in）**：`fx_set_aa(1)` 开启（默认关，避免把一切画布改成离屏/CPU 渲染而影响按直接/GPU 绘制设计的页面，如滚动页）——`fx_draw_line`/`fx_draw_circle`/`fx_fill_circle`/`fx_draw_rect`/`fx_fill_rect_round`/`fx_draw_arc`/`fx_draw_ellipse`/`fx_fill_ellipse` 等按到图形的距离/子像素覆盖做边缘混合，边缘平滑（尤其直线/圆弧/圆角/椭圆）。调 `fx_set_aa(1)` 后**画布自动离屏**以支持混合（无需手动 `fx_canvas_set_buf`），示例 `canvas_07_aa`。
- **渐变填充**：`fx_fill_rect_gradient(x1,y1,x2,y2,c1,c2,vertical)` 在矩形内做 `c1→c2` 线性渐变（vertical=1 上下，0 左右），用于按钮/进度/背景等。
- **离屏缓冲推广到任意画布**：`fx_canvas_enable_buf` 去掉按名字（rt_cv/pt_cv/…）的性能锁，并加 4M 像素内存护栏；任何画布都能 `fx_canvas_set_buf(w, 1)` 使用离屏缓冲。
- **canvas 性能**：
  - `fx_fill_rect` 离屏路径加**逐行直写 offbuf 快刷**（不再逐像素经 `fxtk_put_px`/边界检查），大块填充（棋盘/背景）显著提速。
  - 行缓冲改为**按连续段刷新**（见"圆/圆弧修复"），既修伪影也避免多余像素上传。
- **命名颜色常量**：`FX_WINDOW_BG` / `FX_WINDOW_DARK` / `FX_UI_FG` / `FX_BTN_BLUE` / `FX_OK_GREEN` / `FX_RED_ACCENT` / `FX_PURPLE` / `FX_CANVAS_DARK`，统一各处灰色（消除 240/245 不一致）。
- **CI**：`.github/workflows/ci.yml` — Linux 构建 + 无头测试 + 示例/画布示例 + 警告报告，Windows 交叉编译。

### 修复
- **头文件 include guard 尾部落空**：`fxtk.h` / `fxtk_desktop.h` 的 `#endif` 移到文件末尾，所有声明回到 guard 与 `extern "C"` 内（修复 C++ 链接/重复包含隐患）。
- **数字宏枚举归并**：`FX_W_IMAGE`/`FX_W_TEXTEDIT`/`FX_W_SCROLL`/`FX_A_IMAGE`/`FX_A_MAXLEN` 并入 `fxtk.h` 主枚举，消除 `-Wswitch` 警告。
- **`build_win.sh`**：删除对不存在脚本 `tools/gen_gpu_stub.py` 的依赖，补上缺失的 `fxtk_extra.c`。
- **`strdup` 隐式声明**：`fxtk_extra.c` / `fxtk_font.c` 增加 `_POSIX_C_SOURCE`，严格 `-std` 下可编译。
- **圆/圆弧/椭圆渲染伪影（蝴蝶结）**：行缓冲之前按 `x0..x1` 整段 `push`，会把其它行的旧像素一起带上，导致画布上的圆/圆弧/椭圆出现“实心/蝴蝶结”。改为**按连续段刷新**（断点即刷），圆/圆弧恢复为干净描边。（用 `demo-main/test/render_canvas.c` 无头渲染复现并验证。）
- **画布随窗口缩放**：canvas 示例内容全部改为 `cw`/`ch` 比例坐标，窗口任意缩放都跟随、不挤角、不空白；无头渲染验证 480/800/200 三尺寸不崩坏。
- **窗口放大后 canvas 空白 / 控件消失（根因修复）**：`fxtk_sdl_driver.c` 的 `sdl_apply_size` 在 resize 时先把 `fb_w/fb_h` 设成新尺寸、**再**释放 `fb_rgba`，导致 `fb_ensure` 判定"尺寸未变"而**永不重新分配** `fb_rgba`；随后 `sdl_push_pixels` 因 `fb_rgba==NULL` 直接丢弃所有**软件渲染像素**（离屏 blit、圆/圆弧/描边/文本等），于是放大后 canvas 空白、部分控件消失。改为释放后置 `fb_w=fb_h=0`，让 `fb_ensure` 按新尺寸重分配。该修复覆盖 canvas03 的"放大空白"与 canvas04 的"转盘/描边消失"（软件像素在 resize 后不再被丢弃）。
- **离屏缓冲（+ 缩放限制说明）**：离屏画布在窗口**放大**时受驱动器/合成路径限制可能出空白（demo 的 3D 页也为此主动关离屏）。已在 canvas_03 标注，离屏建议用于固定尺寸/静态内容；`fxtk_draw.c` 的 `fx_canvas_enable_buf` 推广到任意画布并加 4M 护栏。
- **示例**：`ex16_dynamic` 动态按钮改用独立点击回调（修复复用画布回调导致的错位）；`ex02_widgets` 更正“grid 默认黑色”的错误注释；demo 键盘按钮删除冗余 `call()+fx_set_cb`。

### 文档
- `docs/guide.md` / `api.md` / `internals.md`：修正颜色表（16-bit vs 24-bit）、`fx_list_new_p`/`fx_drop_new_p` 第三参（页签页码）、默认缩放 2.5→1.6、默认背景 240→245、`uint16_t`→`fx_color_t`、`fx_delete` 用法（`fx_wptr`），并补写主题/滚动容器/GPU 光追三节。
- 新增画布教程说明于 `docs/examples.md`。

### 工程化
- 统一 `demo-main/Makefile`（demo/examples/test 单一源清单）。
- 无头单元测试 `demo-main/test/headless_test.c`（布局/命中/value/计数/查找删除/画布 API）。

## v2.1（历史）
- 桌面扩展完善、渲染引擎（光追/贴图/后处理）、动态压测、12 页演示。
