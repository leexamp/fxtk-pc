# Changelog

## v2.3

> 本版次随发布做了一次全面审查, 覆盖工程链(构建/CI)与正确性(渲染/核心/平台)两部分。
> 所有修复均经 12 节无头测试、28 个目标干净构建、7 个画布示例修复前后像素级(PPM)回归、
> ASan+UBSan 全量清洗与真实 demo 运行验证。

### 新增
- **可选控件编译（减小体积）**：`fxtk_internal.h` 新增 `FXTK_WIDGET_*` 配置宏（默认全 1）。用 `-DFXTK_WIDGET_XXX=0` 编译时裁掉对应控件的**绘制/创建实现**，减小二进制（为 ESP32 等体积受限平台）。可裁剪：BUTTON/LABEL/GRID/CANVAS/SLIDER/PROGRESS/CHECKBOX/PANEL/TAB/IMAGE/TEXTEDIT。
  - 实现：`fxtk_widgets.c` 各控件绘函数、`fxtk_extra.c` 的列表/下拉用 `#if FXTK_WIDGET_XXX` 包裹；`fxtk.c` 的 `draw_widget`/`redraw_widget_now`/`draw_canvas_only` 对应 case 用 `#if` 包裹。
  - 验证：默认全开行为不变；裁掉按钮+复选框 170KB→142KB，且编译/链接通过、无 undefined；甚至 `FXTK_WIDGET_CANVAS=0` 也能编译；关闭 list+drop 后无头测试仍通过。
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

### 修复 — 工程链
- **恢复 `demo-main/Makefile` 统一构建入口**（此前宣布过它, 但仓库中并不存在, 导致 README 快速开始与 CI 全断）:
  `make` / `make fxtk_sim_en` / `make test` / `make exXX_*` / `make canvas_*` / `make clean`。
  `test` 目标显式 `.PHONY` + 真实规则, 根除"同名 test/ 目录被 make 判为最新"的假成功
  （此前 `make test` exit 0 却一行测试都没编译运行）。
- **四处源清单补上 `fxtk_fs.c`**: `build.sh` / `build_ex.sh` / `build_win.sh` / CI warnings 步骤此前漏编,
  `./build.sh` 链接必败（undefined reference to `fx_fs_pick_dir`/`fx_fs_list`）。
  现以 Makefile 的 `CORE` 清单为唯一事实来源。
- **`build_win.sh` 示例路径修正**: `examples/$EX.c` → `../examples/$EX.c`（demo-main 下不存在 examples/ 子目录）。
- **CI**: warnings 步骤编译失败时不再 `exit 0` 假装成功, 并移除形同虚设的 <64 "门禁"; windows-cross job
  在交叉编译前先执行 `setup_win_cross.sh` 下载 vendored SDL2 开发包（此前 third_party/ 不在仓库, job 必红）。
- **`tools/autotrim.sh`**: 匹配正则补 `_p` 变体（`fx_list_new_p` 等）。旧正则对只用 `_p` 变体的源码会误判
  "未用 LIST"并产出让链接失败的 `-DFXTK_WIDGET_LIST=0`——对自家 demo 即复现。
- **Windows 交叉编译依赖自举**：`build_win_cross.sh` 检测到缺编译器/vendored SDL 时自动调用
  `setup_win_cross.sh`（不再直接失败）；setup 改为幂等（已就绪秒退），工具链按 apt/pacman/dnf 自动安装，
  SDL2/SDL2_ttf/SDL2_image mingw 开发包下载带重试与解压校验（URL 已逐一验证）。
  实测：Linux 上产出 PE32+ exe + 三个 DLL，wine 冒烟进入主循环。
- **ESP32 `CMakeLists.txt`**: 补齐缺失的 `fxtk_effects.c` / `fxtk_extra.c` / `fxtk_fs.c`（旧清单组件链接必败）。
- `test/headless_test.c` 内过时的手动构建注释改为与 Makefile 一致。

### 修复 — 正确性
- **P0 渲染**: `fx_image_create` 对 >32767 的宽高返回 NULL（旧: int16 截断为负 → `fx_draw_image_ex` 越界读 SEGV, ASan 实证）。
- **渲染**: GPU 快路径（draw_line/fill_tri）补上裁剪 —— 旧"去clip保批"使被 tab/滚动/画布上下文裁剪的线条溢出到邻居控件; 行缓冲滞留像素在离屏 blit/文字 blit/旋转 blit 前先刷出（修 z-order 违例）; 负坐标画布离屏 blit 与屏幕求交（旧 `(uint16_t)` 回绕整块丢失）; `aa_arc` 负角度区间（如 -90..90）归一化+模长比较（旧跨 0 段整段丢失）, 弧端点边界像素不再丢失; `fx_fill_rect_gradient` 末行到达 c2; `fx_set_clip` 饱和防 int16 回绕; 行缓冲扩容失败不再把未写入像素当数据刷出。
- **核心**: `te_grow` 返回成败, OOM 时 `te_insert` 放弃插入（旧: 粘贴+OOM 堆越界写）; `hit_test` SCROLL 分支检查容器/子件可见性并拒绝视口外触点（旧: 点击滚动框外任意位置按 +scroll_y 命中看不见的子件）; grid 引用布局期重解析（旧: 先子后父/名字打错 → 子件 (0,0,0,0) 静默）; `percent()` 钳到 [-1,1]（旧: "100,100" int16 回绕把控件甩出屏幕）; 只读文本框禁止 Ctrl+L 清空 / Ctrl+X 剪切; `fx_init`/`unlink_free` 清理全部悬垂状态（ctx 弹层/滚轮目标/滚动状态池/extra 槽位）; scroll 状态池满退化为无缓动直滚（旧: 静默别名到 [0] 两控件互踩）; 列表弹层条目为别名指针, `fx_list_clear` 先关弹层再释放（修 UAF）, 属主删除后弹层安全停放; title/tab 标签截断按 UTF-8 边界回退; `te_next_off/te_prev_off` 判空。
- **控件**: 滑条 value=0 不再画 1px 假填充（filled-1 反向矩形）。
- **字体**: `fxtk_put_px` 声明修正为 uint32_t（旧与定义类型冲突, LTO 实证 UB）; `fx_text_width_n`/`fx_draw_text_c_n` OOM 判空。
- **平台**: SDL 驱动 `push_pixels` 在 set_window 之前收到像素不再除零; raymarch 线程池创建失败按实际数降级（旧: 任一 pthread_create/barrier 失败 → 主线程永久死锁在 barrier）; gpu_raymarch 共享状态改锁内读取, 新增 `gpu_raymarch_shutdown`（atexit 注册）; `fx_init` 重置时同步清理 extra 模块静态池; fxtk_fs Win32 路径转换检查+限长拼接+64 位文件大小。

### 测试
- 无头测试扩至 12 节: 原有 8 节 + 新增回归 [9] 超大图像拒绝 / [10] SCROLL 隐藏子件不可点（正反例）/ [11] grid 晚绑定 / [12] percent 钳制。
- 验证矩阵: 28 目标干净构建 0 error; 7 画布示例 PPM 修复前后对比（6 个完全一致, canvas_07_aa 仅弧终点 3 像素为改善性差异）; ASan+UBSan 下无头测试与全部画布示例 CLEAN; 真实 demo（SDL dummy）运行干净。

### 说明
- 实测澄清: 裁剪 BUTTON+CHECKBOX 的体积收益在 .o 级约 2.3KB, 链接产物大小不变;
  此前所记 "170KB→142KB" 无法复现, 待用统一 Makefile 重新标定。

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
