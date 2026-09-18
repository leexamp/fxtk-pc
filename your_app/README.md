# your_app —— 从这里开始写你的应用

最小可运行示例：一个标签、一个按钮、一个滑条。**约 60 行**，全部注释在代码里。

```bash
cd your_app
make && ./your_app
```

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
