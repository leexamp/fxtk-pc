# Windows Build Guide

## Route A: WSL2 + WSLg (recommended, zero changes)
Win11's WSLg natively supports Linux GUI:
```powershell
wsl --install            # admin PowerShell, then reboot
```
```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
cd /media/.../demo-main && ./build.sh   # the window appears on the Windows desktop
```
The window can go fullscreen (maximize via the title bar), and the UI scales proportionally.

## Route B: MSYS2 native exe
1. Install [MSYS2](https://www.msys2.org), open a **MINGW64** terminal;
2. `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-python`
3. `./build_win.sh` → `fxtk_win.exe` (can be distributed independently of MSYS2, needs the SDL2 dll).

Difference notes:
- **Font**: the driver automatically tries `C:/Windows/Fonts/msyh.ttc` (YaHei, includes Chinese).
- **GPU ray tracing**: EGL pbuffer is Linux-only; on Windows `gpu_stub_win.c` automatically substitutes, and the "3D" page runs CPU multi-threaded ray tracing (winpthreads provides pthread).
- **Clipboard/Wheel/IME**: SDL2 natively supports these on Windows; behavior is consistent.

## Route C: MSVC + vcpkg (feasible)
`vcpkg install sdl2 sdl2-ttf sdl2-image pthreads`, using CMake or a hand-built project; the source-file list is the same as `build_win.sh`; use vcpkg's pthreads package for pthread.

## Cross compile (Linux produces a Windows exe)
`./setup_win_cross.sh` installs the toolchain, `./build_win_cross.sh app` → `dist/win/fxtk_win.exe`; the artifact lands directly in the shared folder — double-click it in the VM to run.
