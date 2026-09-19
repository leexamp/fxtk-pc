# 本项目对 vendored sokol 的本地修改

本目录是**入树（vendored）**的上游 [sokol](https://github.com/floooh/sokol) 头文件，版本见 `VERSION.txt`。
本项目在 X11/Linux 桌面路径上做了少量修改。**上游升级时必须重新施加**，因此在这里逐条记录
（改动处在源码中均带 `本项目本地修改` / `v2.4.4` 注释，可用 `grep -n "本项目本地修改" third_party/sokol/*.h` 全部找出）。

| # | 文件 | 位置（约） | 改了什么 | 为什么 |
|---|---|---|---|---|
| 1 | `sokol_app.h` | 13408 | Win32/X11 后端设置**最小窗口尺寸**（480×272） | 设计坐标是 480×272，再小会挤坏布局 |
| 2 | `sokol_app.h` | 13769–13840 | 新增 **XIM（X11 输入法）** 支持：`setlocale` + `XSetLocaleModifiers` + `XOpenIM`/`XCreateIC`（优先 `XIMPreeditPosition`，失败退回 `PreeditNothing`）+ `XSetICFocus` | 上游 X11 后端不支持输入法，中文**完全无法输入** |
| 3 | `sokol_app.h` | 13776 + 13816 | 导出 `sapp_x11_set_ime_spot()`，配合 `XNSpotLocation` 把**输入法候选窗贴到光标处** | 否则候选窗位置随机（用户实测：跑到屏幕角落） |
| 4 | `sokol_app.h` | 14392 | 在事件派发**之前**调用 `XFilterEvent` | 不拦掉组字期间的按键/预编辑事件，输入法会同时往应用里打字 |
| 5 | `sokol_app.h` | 13811 | `XSetLocaleModifiers`：用户设了 `XMODIFIERS` 就传空串遵循它，否则退回 `"@im=fcitx"` | 原先**无条件写死 fcitx**，会覆盖用户的输入法模块选择（ibus 等） |
| 6 | `sokol_app.h` | 13813 / 13834–13835 | 原先无条件 `fprintf(stderr, "[sokol][xim] …")` 的调试输出，改为**仅在 `FXTK_IMEDBG=1` 时打印** | 库不该每次启动都往 stderr 说话 |
| 7 | `sokol_app.h` | 按键处理 | `Xutf8LookupString` 返回的多字节序列**按码点逐个派发** CHAR 事件 | 一个按键可能上屏多个字符（中文/emoji） |
| 8 | `sokol_app.h` | 各处 | 补充 `extern` 声明代替新增 include（如 `setlocale` / `XSetLocaleModifiers`） | 尽量少动上游的 include 结构，便于对照 |

## 许可

- **sokol**：zlib 许可（见各头文件头部，`Copyright (c) 2017 Andre Weissflog`）。本仓库对其修改**同样以 zlib 许可发布**。
- **stb**（位于 `components/fxtk/vendor/`）：public domain / MIT 双许可（见文件头部）。
- 本仓库自身：MIT（见仓库根 `LICENSE`）。

> 说明：这里的修改**没有**以 `.patch` 形式保存，因为无法保证它与上游具体快照逐行对应；
> 采用「源码内标记 + 本表」的方式，配合 `VERSION.txt` 记录的上游 commit，升级时可逐条重新施加。
