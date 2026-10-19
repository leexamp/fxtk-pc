# 多真实窗口（FXTK_MW）— 设计、接口与维护

> 状态：**实验性、独立分支**（`feat/multi-window`）。默认后端仍是单窗口 sokol_app；多窗口是
> 在 `drivers/` 里**换掉平台层**的第二个后端，不影响现有单窗口路径与体积。
> 参考实现/压测用例：MV 仓库 `mv/pv/v2/single/movie.c`（6 个窗口、每帧移动/缩放、81 句画面）。

## 1. 它解决什么

sokol_app 是单窗口设计（全局状态 + 一次 `sapp_run`），做不了真正的多窗口。
本分支提供 **一个进程、一个 GL 上下文、N 个原生窗口** 的后端：

- 应用只包含 `fxtk.h` + `fxtk_win.h`，**不碰 X11/Win32/sokol**；
- 后端实现一个 vtable（`fxtk_win_backend`），Windows 走 WGL、Linux 走 GLX；
- 每个窗口有自己的画布/帧缓冲（尺寸可不同），每帧逐个窗口 `make_current → 画 → SwapBuffers`。

## 2. 文件与职责

| 路径 | 职责 |
|---|---|
| `drivers/fxtk_win.h/.c` | 后端无关 façade：注册 vtable、窗口生命周期、尺寸/位置/标题、屏幕尺寸、客户区偏移 |
| `drivers/fxtk_sokol_mw.c` | 多窗口后端实现（X11 GLX + Win32 WGL） |
| `drivers/fxtk_mw.h` | 后端内部接口（`fxtk_mw_register()`、窗口操作、屏幕尺寸、客户区偏移） |
| `drivers/fxtk_mw_gl.c` | 单窗口 GL 入口（上下文/交换链工具） |
| `drivers/fxtk_sokol_driver.c` | fxtk 的 sokol 驱动；`FXTK_MW` 分支下支持每窗口画布/帧缓冲缓存 |

## 3. 编译

应用统一加 `-DFXTK_MW`；Windows 需要 `-DSOKOL_GLCORE`。

Linux：

    gcc -DFXTK_MW -O2 app.c \
        drivers/fxtk_sokol_mw.c drivers/fxtk_sokol_driver.c \
        drivers/fxtk_font_stb.c drivers/fxtk_win.c components/fxtk/*.c \
        -Icomponents/fxtk -Idrivers -Ithird_party/sokol \
        -o app -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm

Windows（MinGW-w64；Linux 上交叉编译同理加前缀）：

    x86_64-w64-mingw32-gcc -DFXTK_MW -DSOKOL_GLCORE -O2 app.c \
        drivers/fxtk_sokol_mw.c drivers/fxtk_sokol_driver.c \
        drivers/fxtk_font_stb.c drivers/fxtk_win.c components/fxtk/*.c \
        -Icomponents/fxtk -Idrivers -Ithird_party/sokol \
        -o app.exe \
        -lopengl32 -lgdi32 -luser32 -lole32 -loleaut32 -luuid -lshell32 -lwinmm -mwindows

## 4. 应用侧 API（`fxtk_win.h`）

| API | 说明 |
|---|---|
| `fxtk_win_init() / shutdown` | 初始化/关闭当前后端 |
| `fxtk_win_create(w,h,x,y,title)` | 创建窗口，返回 id（失败 -1） |
| `fxtk_win_create_ex(..., flags)` | 带标志创建；`FXTK_WIN_BORDERLESS` = 无边框 |
| `fxtk_win_show(id,on) / raise / move / resize / title` | 窗口生命周期与几何 |
| `fxtk_win_begin(id) / end(id)` | 该窗口一帧的绘制区间（begin 设当前 GL 上下文/尺寸，end 合成并 present） |
| `fxtk_win_width/height` | 客户区像素尺寸 |
| `fxtk_win_poll() / quit() / time()` | 事件轮询、退出查询、单调时间 |
| `fxtk_win_screen(&w,&h)` | 屏幕像素尺寸（应用用来把 1280×720 舞台居中/等比缩放） |
| `fxtk_win_client_offset(id,&ox,&oy)` | 客户区相对外框偏移（Win32 需要；X11 为 0） |

最小流程：

    fxtk_mw_register();                 // 平台外壳注册后端（fxtk_mw.h）
    fxtk_win_init();
    int w1 = fxtk_win_create(640,300, 30,30, "notes");
    int w2 = fxtk_win_create(1200,80, 40,600, "player");
    for (;;) {
        fxtk_win_poll();
        if (fxtk_win_quit()) break;
        fxtk_win_begin(w1);  /* fxtk 绘制 */  fxtk_win_end(w1);
        fxtk_win_begin(w2);  /* fxtk 绘制 */  fxtk_win_end(w2);
    }

要点：

- 绘制坐标是**窗口客户区逻辑像素**；窗口尺寸不同完全没问题（每窗口独立画布）。
- 关闭任意窗口（或按 Esc）= 退出整个程序；如需"关某个窗口不退出"，要改后端 `s_quit` 语义。
- 标题是 UTF-8：Win32 内部转 UTF-16（`CreateWindowExW` + `DefWindowProcW`），X11 写 `_NET_WM_NAME`。
- **应用坐标 ≠ 外框坐标**：`fxtk_win_move` 传的是窗口外框位置；想对齐"客户区"用
  `fxtk_win_client_offset` 自行扣掉（Win32 标题栏约 30–40px，X11 由 WM 处理、偏移返回 0）。

## 5. 后端实现要点（维护者看）

- 一进程一 GL 上下文；每个窗口 `CreateWindow` + 同一个 pixel format，渲染前 `make_current`。
- 每窗口 canvas/framebuffer 按**尺寸**缓存（24 槽）。**不要逐帧改窗口尺寸**：每个新尺寸都会新建
  缓存，连续 resize 会打爆缓存并触发 sokol 校验 panic（表现为闪屏/黑窗/崩溃）。
- **必须关 vsync**：N 个窗口串行 `SwapBuffers`，若开 vsync 会互相等垂直同步，帧率掉到
  `60/N`；帧率由应用自己的 fps 上限控制。
- 窗口事件走原生 API：Win32 `PeekMessageW/DispatchMessageW`（`WM_CLOSE`/`WM_DESTROY`/ESC）；
  X11 `XPending/XNextEvent`（`WM_DELETE_WINDOW`/DestroyNotify/ESC）。
- 已知限制（如实写）：
  - **X11 only**，未做 Wayland；
  - 只覆盖主屏，未处理多显示器；
  - 目前**不把鼠标/键盘事件转发给 fxtk 控件层**（MV 不需要交互；要做交互需补事件队列）；
  - 最大窗口数 `FXTK_MW_MAX = 8`；
  - 真 HiDPI 未验证（沿用 DPI-unaware + 系统缩放）；
  - 没有窗口图标/最小化状态/每窗口独立消息回调。

## 6. 验证

- MV 用例：`mv/pv/v2/single/`，6 窗口 + 每句动画 + 全片 212s（含窗口缩放/移动/闪白），跑通即说明后端可用；
- 冒烟：编译后运行它，检查（a）多窗口标题栏与内容正常（b）任意窗口关闭能退出（c）连续 resize 不闪/不崩；
- 见 MV 仓库 `mv/pv/v2/single/MAINTENANCE.md` 第 5、6 节的"已知坑/回归命令"。

## 附：与画布内窗口管理器（`fxtk_wm`）的区别

- 本文件 = **系统真实窗口**（一进程 N 原生窗口，OS 标题栏/任务栏/焦点）；
- `fxtk_wm` = **单窗口内画出来的窗口**（自绘标题栏/边框/合成，不占系统窗口）；
  需要"看起来像窗口"而不是"真的是窗口"时用它，见 [window-manager.md](window-manager.md)。

