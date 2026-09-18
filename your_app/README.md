# your_app —— 从这里开始写你的应用

最小可运行示例：一个标签、一个按钮、一个滑条。**约 60 行**，全部注释在代码里。

```bash
cd your_app
make && ./your_app
```

## 构建形态（都不需要额外配置）

| 命令 | 产物 | 说明 |
|---|---|---|
| `./build.sh` | `your_app` 173K | Linux 单文件，默认 `-Os` |
| `./build.sh --autotrim` | 157K | 按 `main.c` 实际用到的控件裁掉其余（体积更小） |
| `./build.sh --shared` | `your_app` **15K** + `libfxtk.so` + `libfxtk_sokol.so` | exe 只留应用层（注意：sokol 实现必须在 `.so` 里，别编进 exe） |
| `./build_win.sh` | `dist/win/your_app.exe` | **在 Linux 上交叉编译**出 Windows 单 exe；只依赖系统 DLL，无 DLL 需要分发 |
| `./build_win.sh --autotrim` | 更小的 exe | 同上 + 控件裁剪 |
| `./build.sh -O2` | — | 换优化级别（默认 `-Os`） |

Windows 交叉编译需要 mingw-w64：`sudo apt install gcc-mingw-w64-x86-64`（或跑 `../demo-main/setup_win_cross.sh`）。
验证依赖：`x86_64-w64-mingw32-objdump -p dist/win/your_app.exe | grep 'DLL Name'`

## 三个文件就够

| 文件 | 作用 |
|---|---|
| `main.c` | 你的应用：初始化 → 建控件 → 每帧 `fx_poll()` + `fxtk_sokol_frame()` |
| `Makefile` | 编译框架核心（`../components/fxtk`）+ 你要用的驱动（`../drivers`） |
| 本文件 | 说明 |

## 改起来最快的几处

- **加控件**：照抄 `init_cb()` 里的那几行。所有控件都是 `fx_xxx_new(属性...)` 形式，
  坐标用 `pixel("x1,y1","x2,y2")`（480×272 设计坐标，窗口缩放自动等比放大）。
- **响应交互**：控件的 `call(on_xxx)` 就是回调，函数签名固定为 `void f(fx_widget_t *w, void *ud)`。
- **换配色/圆角/留白**：不要逐处改控件，改 `../components/fxtk/fxtk_tokens.h` 一处即可全局生效。
- **换后端**：把 `Makefile` 里的 `DRV` 换成 `../drivers/fxtk_sdl_driver.c`（遗留对照）即可；
  接口是同一套 `fx_driver_t`（见 `docs/internals.md`）。
- **想调性能/排版**：`make -C ../demo-main bench`、`golden`；跑不动就翻 `docs/guide.md`。

## 注意事项

- 需要系统有 **OpenGL + X11**（`libgl1-mesa-dev libx11-dev libxcursor-dev libxi-dev`）。
- 中文字体默认取系统里的（找不到会自动回退），界面文字用内置字体渲染，不依赖系统 UI 库。
- 本目录**不参与**主构建，删掉也不影响框架与演示。
