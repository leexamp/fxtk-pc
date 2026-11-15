# fxtk 内部机制

面向想改源码/移植的人。应用开发看 `guide.md`。

## 模块划分

| 文件 | 职责 |
|---|---|
| `components/fxtk/fxtk.c` | 控件树、布局、输入分发、重绘调度（脏区/整帧）、弹层、默认配色 |
| `components/fxtk/fxtk_widgets.c` | 各控件绘制（按钮高光/标签页立体/网格等） |
| `components/fxtk/fxtk_extra.c` | 列表/下拉/滚动等扩展控件 |
| `components/fxtk/fxtk_font.c` | **SDL/ttf 文本后端**：字体缓存 + 文本纹理缓存（无头测试/金图用） |
| `drivers/fxtk_font_stb.c` | **sokol/stb 文本后端**：字形 rasterize + advance 缓存 + 文本纹理缓存 |
| `components/fxtk/fxtk_draw.c` | 绘制原语、`fxtk_text_blit`、裁剪、AA、离屏缓冲 |
| `components/fxtk/fxtk_effects.c` | 图像特效 |
| `drivers/fxtk_sdl_driver.c` | SDL2 窗口/渲染/输入/resize（无头基准/金图专用假驱动） |
| `drivers/fxtk_sokol_driver.c` | **默认后端**：sokol_app + OpenGL 顶点批/命令流/持久画布纹理 |
| `demo-main/raymarch.c` | CPU 光追（持久线程池 + barrier） |
| `demo-main/gpu_raymarch.c` | GPU(EGL/GLES2) 光追后台线程 |

## 对象模型

`fx_widget_t`（`fxtk_internal.h`）含：

- `type/flags/pos_mode`：类型 / 标志（可见、anim、dense、fit、readonly…）/ 布局模式
- 布局源：像素 `ox1..oy2`、百分比 `px1..py2`、网格 `gr1..gc2 + grid_ref`
- `title/name/cb/ud/bg/fg/border/radius`
- `lines`(字号) / `rows`(对齐) / `value` / `page`
- 计算后矩形 `x1..y2`；父子/兄弟指针

**控件池**：静态数组 `s_pool[FX_MAX_WIDGETS]`，`FX_MAX_WIDGETS`（**PC 默认 8192 ≈ 2.6MB BSS**；`sizeof(fx_widget_t)` = 328B）。`fxtk_alloc()` 线性找空槽，`fxtk_free()` 标记 `FX_W_NONE`。

> **内存账（PC / sokol 版实测）**：整个二进制 `.bss` 约 **2.9MB**，其中控件池占 2.6MB。
> sokol 驱动的顶点/索引/命令缓冲是**按需增长**的堆缓冲：初始 16k 顶点（≈590KB）/ 32k 索引（≈128KB）/
> 1k 命令，帧内需求超过容量才翻倍（硬上限仍是 `VB_MAX=262144` / `IB_MAX` / `CMD_MAX=65536`），
> GPU 侧的 `sg_buffer` 也只在容量增长时重建。实测演示峰值仅 5k~15k 顶点，绝大多数应用不会触发增长。
> 初始容量可用 `-DVB_INIT=... -DIB_INIT=... -DCMD_INIT=...` 覆盖（测试强制走增长路径用）。
> 另有一张**持久画布纹理**（窗口尺寸 RGBA8，1280×720 ≈ 3.7MB）——脏区局部重绘依赖它跨帧保留内容。


## 布局与缩放

- 基线默认 480×272（变量 `fx_design_w/h`，由 `fx_set_design_pixel()` 改）；
  `s_sx1000 = width*1000/fx_design_w`、`s_sy1000` 同理。
- `FX_POS_PIXEL`：设计坐标 × 比例；`FX_POS_PERCENT`：父矩形 × 浮点；`FX_POS_GRID`：命名网格。
- **`FX_POS_FIXED`**：布局跳过（`case FX_POS_FIXED: break;`），坐标直写。弹层与动态控件用。
  - `fx_widget_fix(w,x,y)` 把控件切到 FIXED 并记录基准 `ox1/oy1`，供后续自绘移动。
- resize 时 `sdl_apply_size` 更新比例并 `fx_layout()` + 全量重绘。

## 默认配色（fxtk.c `fx_widget_new_impl`）

- 窗口背景 `s_bg = FX_RGB(245,245,245)`。
- 所有控件 `fg` 默认 `FX_RGB(40,40,40)` 深字；`bg` 按类型：
  - label/checkbox/image：`FX_BLACK` 哨兵 → 绘制时用窗口背景（透明）
  - grid/panel/tab/scroll：浅底 + `FX_LGRAY`
  - slider/progress：绿填充 + 浅灰轨道
  - textedit：`FX_BLACK` 哨兵 → 白底黑字
  - button/默认：蓝底 `FX_RGB(33,150,243)` + 白字

> `FX_BLACK` 在 bg 里是"未指定"哨兵（label 透明、textedit 白底），显式想用黑底请用 `FX_RGB(0,0,0)`。

## 重绘模型

- **脏区局部重绘（v2.5.6 恢复）**：`fx_repaint_rect` 把矩形并入 `s_dirty[]`（≤8 块，重叠合并，爆表退回整帧）；
  `fx_poll()` 在分支选择**之前**给 `FX_F_ANIM` 控件每帧标脏（画布回调按调用次数推进相位，漏画一帧就会与整帧永久分叉），
  然后逐块 `redraw_region()`：先用背景铺该矩形，再让控件树按该矩形裁剪重画。
- **两个坑（已修，别再犯）**：① tab 画子页时不能把裁剪重置成自身整 rect —— 必须与传入脏区求交，
  否则脏区外的兄弟控件被无背景擦除地重画、AA 文字逐帧叠加变深；② 消费脏区前必须**快照并清零**，
  重绘期间回调产生的新标脏归入下一帧 —— 边消费边追加会让同一画布一帧被画多次、相位超速。
- `FXTK_FULL_REDRAW=1` 退回"任何请求都整帧"；正确性由 5 场景 370 帧整帧/脏区逐像素 A/B 守住。
- sokol 后端：绘制进**持久画布纹理**（`LOAD` 保留上一帧静态内容，脏区帧只重画变动块），
  命令流按管线 + 纹理 + **裁剪矩形**合并（裁剪不同不合并，否则 scissor 会把图形裁掉），最后 blit 到 swapchain。
- SDL 假驱动（无头基准/金图）：渲染到 `target` 纹理，present 整体拷贝；像素路径写 `fb_rgba` 脏区后单次上传。

## 字体与文本缓存

- **字号 → 设备像素**：`size` 是逻辑字号，两个后端统一按 `设备字号 = size × fx_dpi_scale()` 取字体/测宽/绘制
  （HiDPI 下物理帧缓冲翻倍，字号必须同步翻倍，否则文字相对控件过小；`dpi=1` 时逐字节不变）。
- SDL/ttf 后端（`components/fxtk/fxtk_font.c`）：缓存 `FC_N=8` 个 TTF；满则**淘汰并关闭**旧字体
  （`tc_drop_font` 清理关联文本纹理），杜绝句柄泄漏。
- sokol/stb 后端（`drivers/fxtk_font_stb.c`）：`FONT_MAX=8` 个字号槽，每槽带 **1024 项 advance 直接映射缓存**
  （`tag = cp+1`，省掉每次测宽的字形 cmap 二分查找）；文本纹理缓存 128 项。
- 文本纹理键为 `字体|文本|fg|bg`，LRU 淘汰；GPU 纹理与 CPU 像素两份都随淘汰释放。

## 输入

- 驱动 `touch_read` 每帧回报；`s_last_tx/ty` **始终更新**（悬停可用）。
- 按下/移动/释放分发到最顶层命中控件；弹层优先捕获。
- 键盘入队 `g_kq`（64 项 FIFO）；滚轮累积 `g_wheel_pix`。
- 功能键映射：SDL sym → `FX_KEY_*`；Ctrl+Shift 状态进 `mod` 位。
- **统一滚动（v2.5.5）**：`fx_poll` 把滚轮增量路由到光标下最近的 `FX_W_SCROLL/FX_W_TEXTEDIT/FX_W_CANVAS`
  （画布保持旧语义，回调可自己 `fx_wheel_take` 处理缩放），控件在自己的绘制里调用
  `fxtk_scroll_step()` 消费增量并推进缓动；触摸拖动由 `s_touch_sc` 候选 + 8px 阈值接管，
  松手速度存 `vel` 做惯性。状态池 `s_scroll_pool[FX_MAX_SCROLL_STATES]` 按控件指针索引，
  池满时退化为"无缓动直滚"并 LOG_WARN（不会两个控件互踩）。
- **触摸屏判定**：`fx_touch_capable()` 首次调用时检测并缓存（Linux `/sys/class/input/*/device/properties`
  的 `INPUT_PROP_DIRECT`，触摸板是 `INPUT_PROP_POINTER` 不算；Windows `GetSystemMetrics(SM_MAXIMUMTOUCHES)`）；
  `fx_set_touch_mode()` / `FXTK_TOUCH` 可强制覆盖。桌面没有触摸屏时拖动滚动完全不启用。

## 弹层

下拉/右键菜单为顶层 overlay，最后绘制、不被页面裁剪；点击外部关闭；靠底自动向上弹。

## 驱动契约（`fx_driver_t`）

提供 `width/height`、`set_window/push_pixels`、`fill_rect`、`touch_read/key_read`、`blit_img/blit_tex/fill_tri/draw_line/blit_img_rot` 等；核心不依赖具体平台，替换驱动即可移植。

## 线程

- `raymarch.c`：POSIX 持久线程池（`pthread_barrier` 每帧两扇同步，唤醒 worker 渲染行带），避免每帧 create/join；Windows 退回每帧 create/join（winpthreads 无 barrier）。
- `gpu_raymarch.c`：独立线程做 EGL/GLES2 离屏渲染（拒绝 llvmpipe 软件 GL），任务队列用 mutex+cond。

## v2.5：驱动与多窗口内部变化

- `drivers/fxtk_sokol_driver.c` 增加 `FXTK_MW` 分支：每窗口画布（`s_mw_img[24]`）与帧缓冲（`s_mw_fb[24]`）
  按尺寸缓存；`MW_NEED_CLEAR` 控制首帧/换尺寸清屏；`rgba_pack()` 保留 alpha（24 位 alpha=0 → 不透明）。
- 顶点/索引/命令缓冲：`VB_MAX/IB_MAX/CMD_MAX` 是**硬上限**（262144 / 524288 / 65536）；
  v2.5.6 起改为**按需增长**（初始 16k / 32k / 1k），复杂帧不再静默丢绘制，同时不再常驻约 11MB。
- 多窗口环境：窗口尺寸/位置/事件由 `drivers/fxtk_sokol_mw.c` 负责；`fxtk_sokol_frame()` 只负责画布合成 +
  swapchain 呈现；vsync 关闭由后端完成。
- 单窗口路径保持原样（`SOKOL_APP_IMPL` 仅在非 `FXTK_MW` 时编译）。

## 驱动接口 `fx_driver_t` 逐字段（写后端）

| 字段 | 契约 |
|---|---|
| `width/height` | 画布像素尺寸（必填）。 |
| `init` | 初始化，返回 0 失败。 |
| `set_window(x0,y0,x1,y1)` | 告知当前绘制窗口（裁剪/统计）。 |
| `push_pixels(px,n)` | 像素流推送（软后端）。 |
| `hold_begin/hold_end` | 批量绘制包裹（GPU 合批）。 |
| `fill_rect` | 矩形填充（核心快路径）。 |
| `touch_read(&x,&y,&pressed)` / `key_read(ev)` / `wheel_read(&x,&y,&dy)` | 输入读取（返回 1 有事件）。 |
| `clip_set/clip_get` | 系统剪贴板。 |
| `set_title` | 窗口标题。 |
| `blit_img` / `blit_tex` / `blit_img_rot` | 图片缩放 blit / 文字纹理 blit / 旋转 blit。 |
| `set_clip_rect` | 裁剪。 |
| `fill_tri / draw_line` | GPU 三角/折线批处理。 |
| `read_pixels` | 回读帧（截图/金图）。 |
| `draw_image_quad` | GPU 真透视四边形（缺省走 CPU 逆单应）。 |
| `raymarch` | GPU 光线步进。 |
| `fill_rect_round / stroke_rect_round` | GPU SDF 圆角填充/描边。 |
| `draw_line_aa` | GPU 羽化线段。 |
| `ime_pos` | 输入法候选窗位置。 |

> 除 `width/height` 外全部可为 NULL，框架自动回退 CPU。完整 API 参考见 [api.md](api.md) 第 20 节；
> 多窗口驱动细节见 [multi-window.md](multi-window.md)。
