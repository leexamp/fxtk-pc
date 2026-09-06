# Windows 编译指南

## 路线 A：WSL2 + WSLg（推荐，零改动）
Win11 的 WSLg 原生支持 Linux GUI：
```powershell
wsl --install            # 管理员 PowerShell, 重启
```
```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
cd /media/.../demo-main && ./build.sh   # 窗口直接弹在 Windows 桌面上
```
窗口可全屏（标题栏最大化），界面等比放大。

## 路线 B：MSYS2 原生 exe
1. 装 [MSYS2](https://www.msys2.org)，打开 **MINGW64** 终端；
2. `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-python`
3. `./build_win.sh` → `fxtk_win.exe`（可脱离 MSYS2 分发，需带 SDL2 dll）。

差异说明：
- **字体**：驱动自动尝试 `C:/Windows/Fonts/msyh.ttc`（雅黑，含中文）。
- **GPU 光追**：EGL pbuffer 仅 Linux；Windows 下 `gpu_stub_win.c` 自动顶替，
  「3D」页走 CPU 多线程光追（winpthreads 提供 pthread）。
- **剪贴板/滚轮/IME**：SDL2 在 Windows 原生支持，行为一致。

## 路线 C：MSVC + vcpkg（可行）
`vcpkg install sdl2 sdl2-ttf sdl2-image pthreads`，CMake 或手建工程，
源文件列表同 `build_win.sh`；pthread 用 vcpkg 的 pthreads 包。

## 交叉编译（Linux 出 Windows exe）
`./setup_win_cross.sh` 装工具链，`./build_win_cross.sh app` → `dist/win/fxtk_win.exe`，
产物直落共享文件夹，虚拟机双击即玩。
