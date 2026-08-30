# fxtk 内部机制

面向想改源码/移植的人。应用开发看 `guide.md`。

## 模块划分

| 文件 | 职责 |
|---|---|
| `components/fxtk/fxtk.c` | 控件树、布局、输入分发、重绘调度、弹层、默认配色 |
| `components/fxtk/fxtk_widgets.c` | 各控件绘制（按钮高光/标签页立体/网格等） |
| `components/fxtk/fxtk_extra.c` | 列表/下拉/滚动等扩展控件 |
| `components/fxtk/fxtk_font.c` | TTF 字体缓存 + 文本纹理缓存 |
| `components/fxtk/fxtk_draw.c` | 绘制原语、`fxtk_text_blit`、裁剪 |
| `components/fxtk/fxtk_effects.c` | 图像特效 |
| `demo-main/fxtk_sdl_driver.c` | SDL2 窗口/渲染/输入/resize |
| `demo-main/raymarch.c` | CPU 光追（持久线程池 + barrier） |
| `demo-main/gpu_raymarch.c` | GPU(EGL/GLES2) 光追后台线程 |

## 对象模型

`fx_widget_t`（`fxtk_internal.h`）含：

- `type/flags/pos_mode`：类型 / 标志（可见、anim、dense、fit、readonly…）/ 布局模式
- 布局源：像素 `ox1..oy2`、百分比 `px1..py2`、网格 `gr1..gc2 + grid_ref`
- `title/name/cb/ud/bg/fg/border/radius`
- `lines`(字号) / `rows`(对齐) / `value` / `page`
- 计算后矩形 `x1..y2`；父子/兄弟指针

**控件池**：静态数组 `s_pool[FX_MAX_WIDGETS]`，`FX_MAX_WIDGETS = 4096`（约 940KB BSS）。`fxtk_alloc()` 线性找空槽，`fxtk_free()` 标记 `FX_W_NONE`。

## 布局与缩放

- 基线 480×272；`s_sx1000 = width*1000/480`、`s_sy1000` 同理。
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

- 默认脏区；当前任何 `fx_repaint_rect` 提升为全量（`s_full=1`），保证无残影。
- 渲染到 `target` 纹理（尺寸=逻辑宽高），present 时整体拷贝到后备缓冲。
- resize 重建 `target` 并清屏，帧末 `SDL_RenderSetClipRect(NULL)` 防 clip 残留。
- 图形走**顶点批**：矩形/三角/折线合并为 `SDL_RenderGeometry` 一次提交；`sdl_fill_rect` 并入批（不打断 draw call）。
- 像素路径（离屏/3D）写 `fb_rgba` 脏区，`SDL_UpdateTexture` 整帧单次上传。

## 字体与文本缓存

- `fxtk_font_size(size)`：缓存 `FC_N=8` 个 TTF；满则**淘汰并关闭**旧字体（`tc_drop_font` 清理关联文本纹理），杜绝句柄泄漏。
- 文本纹理缓存 256 项，键为 `字体|文本|fg|bg`，LRU 淘汰。

## 输入

- 驱动 `touch_read` 每帧回报；`s_last_tx/ty` **始终更新**（悬停可用）。
- 按下/移动/释放分发到最顶层命中控件；弹层优先捕获。
- 键盘入队 `g_kq`（64 项 FIFO）；滚轮累积 `g_wheel_pix`。
- 功能键映射：SDL sym → `FX_KEY_*`；Ctrl+Shift 状态进 `mod` 位。

## 弹层

下拉/右键菜单为顶层 overlay，最后绘制、不被页面裁剪；点击外部关闭；靠底自动向上弹。

## 驱动契约（`fx_driver_t`）

提供 `width/height`、`set_window/push_pixels`、`fill_rect`、`touch_read/key_read`、`blit_img/blit_tex/fill_tri/draw_line/blit_img_rot` 等；核心不依赖具体平台，替换驱动即可移植。

## 线程

- `raymarch.c`：POSIX 持久线程池（`pthread_barrier` 每帧两扇同步，唤醒 worker 渲染行带），避免每帧 create/join；Windows 退回每帧 create/join（winpthreads 无 barrier）。
- `gpu_raymarch.c`：独立线程做 EGL/GLES2 离屏渲染（拒绝 llvmpipe 软件 GL），任务队列用 mutex+cond。
