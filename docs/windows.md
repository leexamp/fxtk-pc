# Windows（交叉编译，sokol 单 exe）

> v2.4 起 Windows 侧只推荐这一条路：**sokol 单 exe，不依赖任何第三方 DLL**。
> （旧的 SDL 交叉编译路径已降级为遗留方案，见文末。）

## 一、准备工具链（MSYS2 或 Linux 上的 mingw-w64）

```bash
# MSYS2: 只需要编译器, 不需要任何 SDL 包
pacman -S mingw-w64-x86_64-gcc

# 或 Linux 上直接装交叉编译器
sudo apt install gcc-mingw-w64-x86-64
```
本仓库也带了 `demo-main/setup_win_cross.sh`（一键准备交叉工具链）。

## 二、构建

```bash
cd demo-main
./build_win_sokol.sh            # 中文版 → dist/win_sokol/fxtk_sokol.exe
./build_win_sokol.sh app_en     # 英文版 → dist/win_sokol/fxtk_sokol_en.exe
```

产物约 **433KB / 430KB**，**静态链接**（`-static -static-libgcc`，`-mwindows` 去掉控制台窗口）。

## 三、验证"零第三方 DLL"

```bash
x86_64-w64-mingw32-objdump -p dist/win_sokol/fxtk_sokol.exe | grep 'DLL Name'
```
应当只看到系统 DLL（`KERNEL32.dll` / `USER32.dll` / `GDI32.dll` / `OPENGL32.dll` / `SHELL32.dll` / `ole32.dll` / `dwmapi.dll` 等），
**没有 `SDL2.dll` / `libwinpthread-1.dll` / `libgcc_s_seh-1.dll`**。

## 四、Windows 端的已知限制（如实列出）

| 项 | 状态 |
|---|---|
| 图片导入 / 截图(PNG) | ✅ 可用（stb 编解码，无外部依赖） |
| 键盘/鼠标/滚轮 | ✅ 可用 |
| GPU 形变 / 两层抗锯齿 / 光追 | ✅ 可用（走 OpenGL） |
| **剪贴板** | ✅ 已实现（`OpenClipboard` + `CF_UNICODETEXT`，UTF-8↔UTF-16，支持中文） |
| **中文输入法** | ❌ **不支持**（XIM 是 X11 专有；Windows 需走 IMM32） |
| 中文显示 | ✅ 正常（内置字体，与输入法无关） |

需要剪贴板或输入法时，可临时用遗留的 SDL 交叉编译路径（SDL2 在 Windows 上原生支持这两项）：

```bash
./build_win_cross.sh    # 遗留方案: 需要 mingw-w64 的 SDL2 全家桶, 产物要带 SDL2 dll
```

## 五、发布打包

```bash
make package      # 或 ../tools/package_release.sh
# → dist/pkg/fxtk-<版本>-win-x86_64.zip (中英两个单 exe + README + 运行说明)
```
