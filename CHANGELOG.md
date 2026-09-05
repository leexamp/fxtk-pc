# Changelog

## v2.3（阶段一）

### 新增
- **可选控件编译（减小体积）**：`fxtk_internal.h` 新增 `FXTK_WIDGET_*` 配置宏（默认全 1）。用 `-DFXTK_WIDGET_XXX=0` 编译时裁掉对应控件的**绘制/创建实现**，减小二进制（为 ESP32 等体积受限平台）。可裁剪：BUTTON/LABEL/GRID/CANVAS/SLIDER/PROGRESS/CHECKBOX/PANEL/TAB/IMAGE/TEXTEDIT。
  - 实现：`fxtk_widgets.c` 各控件绘函数、`fxtk_extra.c` 的列表/下拉用 `#if FXTK_WIDGET_XXX` 包裹；`fxtk.c` 的 `draw_widget`/`redraw_widget_now`/`draw_canvas_only` 对应 case 用 `#if` 包裹。
  - 验证：默认全开行为不变；裁掉按钮+复选框 170KB→142KB，且编译/链接通过、无 undefined；甚至 `FXTK_WIDGET_CANVAS=0` 也能编译；关闭 list+drop 后无头测试仍 9/9 通过。
- **标签页侧边栏方位 `sidebar(side)`**：标签条可放上(`FX_TAB_TOP`)/左(`FX_TAB_LEFT`)/右(`FX_TAB_RIGHT`)/下(`FX_TAB_BOTTOM`)四边。英文标签较长时常用左右侧（`FX_TAB_SIDE=88` 宽），顶部/底部用 `FX_TAB_H=24` 高；`fx_draw_tab`/命中/页码计算均按方位适配。用于 v2.3 国际化（英文标签更长）。
- **国际化（i18n）**：新增英文文档 `docs_en/`（README_en/quickstart_en/api_en/guide_en）与英文 demo `demo-main/app_en.c`（左侧边栏放长英文标签）；新增 Makefile 目标 `make fxtk_sim_en` 构建英文 demo；中文保持 `docs/`+`app.c`。英文文件一律 `_en` 后缀。
- **自动裁剪 `tools/autotrim.sh`**：扫描源文件用到的控件工厂宏，自动输出"未使用控件"的 `-DFXTK_WIDGET_XXX=0` 开关，无需手动跟踪（示例 ex01 100KB→88KB）。
- **内存微优化**：文本贴图缓存 `TEXT_CACHE_SIZE` 256→64（省缓存内存，命中不足自动重建）；`FXTK_MAXW` 真正生效（限制窗口上限，防大窗口撑爆窗口尺寸表面/GL 目标）。注：demo 的 ~180MB 驻留基准为 SDL2+GLES2 驱动基线（非泄漏，近 3000 控件压测稳 60 帧）。
- **抗锯齿性能**：`aa_*` 全部改为扫描线（见 v2.2 修复）。
- **跨平台文件 API `fxtk_fs.h/.c`**：`fx_fs_pick_dir(out,cap)` 系统对话框选文件夹（Win32 `SHBrowseForFolderW` / Linux `zenity`）；`fx_fs_list(dir,out,max)` 列目录内容（Win32 `FindFirstFileW` / POSIX `opendir+stat`），返回 name/is_dir/size/date。双平台编译通过；`build_win_cross.sh` 加 `-lshell32 -lole32`。
- **demo 组件页（文件浏览器 + 颜色选择器 + 可拖动组件）**：
  - 左列名字列表编辑器：`fx_grid_map(dense())` 网格排列输入框+"+"，文本框添加+删除，字号滑杆（12~60）缩放"示例缩放文本"。
  - 右列：可拖动组件区（3 盒可拖、clamp 画布内、带边框）；文件夹浏览器（选中文件夹预览 → 表格呈现 名称/大小/日期，竖分隔线、行灰底、长名省略号、**可拖动/滚动的平滑滚动条**（悬停变宽变蓝）、**悬停提示**（跟鼠标显示完整信息）、点击行选中+悬停边框）；`选择文件夹预览` 按钮（跨平台 `fx_fs_pick_dir`）。
- **框架层修复**：tab 子控件裁剪到 tab 自身矩形（防画到 tab 外）；`draw_canvas_only` 的 TAB case 同样裁剪；字号缓存淘汰时**不关正在用的 `g_font`**（防 use-after-free 段错误）；`fx_list_clear`/`fx_textedit_text`/`fx_set_fgcolor_w` 新增。
- **控件页颜色选择器**：两套独立 RGB（背景改按钮底色 / 文字改按钮字色），文本框无 "n/3" 计数，实时更新示例按钮+颜色显示区。
  - 注：关掉的控件仍可创建（枚举/创建宏不变），只是不绘制；请只裁确实不用且跑通的控件。

## v2.2

### 新增
- **画布学习教程** `examples/canvas/`：canvas_01_primitives / 02_immediate / 03_offscreen / 04_custom_widget / 05_image / 06_interact / 07_aa，从图元到交互到抗锯齿逐级讲解，带详尽注释。
  - 所有示例把内容写成 `cw`/`ch` 的**比例坐标**，窗口任意缩放（大/小）内容都跟随画布、不挤角、不越界；并用无头渲染验证了 480x272 / 800x480 / 200x112 都不崩坏。
- **无头渲染工具** `demo-main/test/render_canvas.c`：用假驱动把任意 canvas 示例渲染成 PPM，无需 SDL/窗口/字体即可在不同窗口尺寸下查看/回归（CI 已加入渲染冒烟测试）。
- **便捷画布 API**：
  - `fx_canvas_size(w, &cw, &ch)` — 取画布本地宽高，替代重复的 `fx_widget_rect` + 宽高样板。
  - `fx_canvas_clear(w, color)` — 一键把画布清成指定颜色，替代 `fx_set_color` + `fx_fill_rect` 样板。
- **抗锯齿（平滑渲染，默认关闭/opt-in）**：`fx_set_aa(1)` 开启（默认关，避免把一切画布改成离屏/CPU 渲染而影响按直接/GPU 绘制设计的页面，如滚动页）——`fx_draw_line`/`fx_draw_circle`/`fx_fill_circle`/`fx_draw_rect`/`fx_fill_rect_round`/`fx_draw_arc`/`fx_draw_ellipse`/`fx_fill_ellipse` 等按到图形的距离/子像素覆盖做边缘混合，边缘平滑。调 `fx_set_aa(1)` 后**画布自动离屏**以支持混合（无需手动 `fx_canvas_set_buf`），示例 `canvas_07_aa`。
  - **抗锯齿性能**：`aa_circle`/`aa_fill_circle`/`aa_ellipse`/`aa_fill_ellipse`/`aa_arc`/`aa_fill_rect_round` 由"整框逐像素 sqrt"(O(r²))/SDF 逐像素改为**扫描线**（每行 1 次 sqrt/算角内缩，只扫边缘带/边缘列 + 填充内部），约 **60× 更少 sqrt**，大圆实测 ~9.7ms、含圆角矩形 ~10.7ms/帧且边缘依旧平滑；并移除不再使用的 `sd_round_box`。
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
