# fxtk — 轻量GUI框架 (v2.3)

<p>
  <img alt="version" src="https://img.shields.io/badge/version-v2.3-blue">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
  <img alt="c" src="https://img.shields.io/badge/language-C99-9cf">
  <img alt="platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20ESP32-lightgrey">
</p>

**一个单帧、脏区重绘、属性宏驱动的 C GUI 框架**——核心纯 C、热路径零分配、480×272 响应式设计、
一份头文件就能上手。PC 用 SDL，ESP32 用 `fx_driver_t` 抽象，几乎零改动跨平台。

**画布与抗锯齿（v2.2 无头渲染示意图）**：
| 抗锯齿 `fx_set_aa(1)` | 渐变 `fx_fill_rect_gradient` | 自定义控件（仪表盘） | 棋盘格（缩放安全） |
|---|---|---|---|
| ![aa](screenshot_aa.png) | ![gradient](screenshot_gradient.png) | ![gauge](screenshot_gauge.png) | ![checker](screenshot_checker.png) |

> 注：以上 v2.2 示意图由 `demo-main/test/render_canvas.c` **无头渲染**（不加载字体），只展示图形/渐变/边缘平滑；
> 按钮文字、数值等标签在**真实运行时有**（渲染工具为保持无头把文字函数桩掉了）。完整含文字界面见下方 demo 截图。

完整演示（12 页）见下方 demo 截图：
![graph](graph.png) ![image](image.png) ![rending](rending.png) ![texting](texting.png)

## 特性

- **声明式控件**：`fx_button_new(pixel(...), title(...), call(...))` 属性宏链
- **三种布局**：`pixel()`（480x272 设计坐标，窗口响应式等比缩放）/ `percent()` / `grid()`
- **开箱即用的默认配色**：浅底深字、蓝按钮、绿滑条——不写 `color()` 也能直接看（可显式覆盖）
- **控件集**：按钮 / 标签 / 滑条 / 进度条 / 复选框 / 网格键盘 / 画布 / 标签页 / 图片 / 输入框 / 列表 / 下拉 / 滚动容器
- **立体标签页**：选中页签凸起（亮底深字 + 高光），未选中下凹，阴影分隔线
- **画布立即模式**：线/圆/椭圆/三角/多边形/圆弧/圆角矩形/文字 + **渐变填充** `fx_fill_rect_gradient`，支持离屏缓冲、动画标志 `anim(1)` 与**抗锯齿** `fx_set_aa(1)`（line/circle/ellipse/rect/round-rect/arc 边缘平滑；默认关闭，按需开启，开后该画布自动离屏）。
  v2.2 新增 `fx_canvas_size`/`fx_canvas_clear` 便捷 API，并把离屏缓冲推广到任意画布（去掉按名字的性能锁）。
- **画布学习教程**：`examples/canvas/` 系列（图元 / 立即模式 / 离屏 / 自定义控件 / 图片 / 交互 / 抗锯齿）
- **桌面扩展**：文本框（换行/跨行框选/Ctrl+A C V X 系统剪贴板/`maxlen` 计数/滚轮/光标像素级对齐）、
  滚轮路由、**核心滚动一行调用**（`fx_scroll_update`：目标像素累积 + 25% 逐帧插值，rc 手感；
  状态池多控件并行，静止零重绘）、**滚动条拖拽（含自绘画布）**、焦点管理、
  `fx_widget_set_rect` 运行时移动控件
- **渲染引擎**：`fx_image_*` 24bit RGB 表面、`fx_draw_image_rot` 旋转贴图、图像后处理
  （翻转/灰度/染色/亮度）、多线程软件 Raymarching（SDF 软阴影/AO/雾）+ 可选 GPU(GLSL) 通道
- **性能**：脏区合并重绘、GPU 顶点批、行级持久线程池光追、GPU 呈现（SDL2 加速渲染器）
- **动态压测**：压测页帧率富余时自动生长控件（全屏 1080P 可容数千个），实时显示总控件数
- **工程化**：统一 Makefile 单一源清单、无头单元测试、GitHub CI（Linux + Windows 交叉）
- **体积裁剪（v2.3）**：`-DFXTK_WIDGET_XXX=0` 编译时裁掉用不到的控件，减小二进制（ESP32 等受限平台用，如裁按钮+复选框 170KB→142KB）；`tools/autotrim.sh <源码>` 自动扫描用到的控件并生成这些开关（示例 100KB→88KB）
- **国际化（v2.3）**：英文文档在 `docs_en/`、英文 demo 在 `demo-main/app_en.c`（用左侧边栏标签放长英文标签），中文保持 `docs/` + `app.c`；标签页支持 `sidebar(FX_TAB_TOP/LEFT/RIGHT/BOTTOM)` 四方位
- **跨平台文件 API（v2.3）**：`fxtk_fs.h/.c` 的 `fx_fs_pick_dir()`（系统对话框选文件夹：Win32 `SHBrowseForFolderW` / Linux `zenity`）与 `fx_fs_list()`（列目录：Win32 `FindFirstFileW` / POSIX `opendir+stat`）；demo **组件页**含文件浏览器（平滑滚动条+悬停提示+行选中）、颜色选择器（两套 RGB 改按钮底色/字色）、`fx_grid_map(dense())` 网格名字编辑器、可拖动组件区。

## 目录结构

```
components/fxtk/   核心库 (fxtk.c/draw/widgets/font/effects + 头文件)
demo-main/         PC 模拟器 (SDL2 驱动 / 演示 app / GPU 光追 / examples)
demo-main/Makefile 统一构建 (demo / examples / test 单一源清单)
demo-main/test/     无头单元测试 (无需 SDL/窗口)
.github/            CI (Linux 构建 + 测试 + Windows 交叉)
docs/              文档 (quickstart / guide / api / desktop / effects / examples / internals)
examples/          独立示例 ex01~ex18
examples/canvas/    画布学习教程 canvas_01~canvas_07
screenshot_*.png    v2.2 画布/抗锯齿/渐变示意图
LICENSE            MIT 许可
```

## 快速开始 (Linux)

```bash
# 必要依赖: SDL2 全家桶 + 3D/EGL/X11 (demo 的 3D 页需要)
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
                 libegl1-mesa-dev libgles2-mesa-dev libx11-dev
cd demo-main
./build.sh                # 编译并运行完整演示 (12 个标签页)
./build_ex.sh ex01_hello  # 运行独立示例
./make_release.sh         # 打包正式版 fxtk-v2.1.tar.gz
```

也可以用统一入口 `Makefile`（单一源清单）：

```bash
cd demo-main
make                       # 构建完整演示 fxtk_sim
make test                  # 构建并运行无头单元测试 (无需 SDL/窗口/字体)
make ex01_hello            # 构建指定示例
make clean
```

## 测试与 CI

- 无头单元测试 `demo-main/test/headless_test.c` 用一个假 `fx_driver_t` 直接驱动核心库，
  验证 **pixel/percent/grid 布局**、**press/release 命中回调**、**value 读写**、
  **控件计数** 与 **fx_find/fx_delete**，不初始化 SDL/窗口/字体。
- CI (`.github/workflows/ci.yml`) 在 Linux 上构建 demo + 跑测试 + 构建示例，
  并用 vendored `third_party/SDL2-win` 做 Windows 交叉编译。

## 演示页速览

波形(动画画布) / 图形(旋转贴图+矢量动效) / 控件(网格键盘) / 图片(缩放/切换) /
3D(CPU 多线程光追, 超频+GPU 开关) / 输入(文本框全家桶) / 画板(鼠标作画) /
键鼠(事件监视) / 压测(动态控件生长) / 滚动(长列表+滚动条) / 组件(列表/下拉) / 粒子(万级图元)

## 发布

```bash
cd demo-main
./make_release.sh      # → fxtk-v2.3.tar.gz           (源码包: 核心库+模拟器+全部示例+文档/许可/CI)
./make_bin_release.sh  # → dist/fxtk-v2.2-bin-*.tar.gz/.zip (Linux demo + Windows 交叉二进制)
```

## 文档

- [docs/quickstart.md](docs/quickstart.md) — 5 分钟跑通
- [docs/guide.md](docs/guide.md) — 从零到精通完整教程
- [docs/api.md](docs/api.md) — API 速查表
- [docs/desktop.md](docs/desktop.md) — 桌面演示验收点
- [docs/effects.md](docs/effects.md) — 图片/特效说明
- [docs/examples.md](docs/examples.md) — 示例导读
- [docs/internals.md](docs/internals.md) — 内部机制（面向源码修改）
- [docs/windows.md](docs/windows.md) — Windows 编译

## 许可

MIT（示例与文档同许可）。全文见 [LICENSE](LICENSE)。
