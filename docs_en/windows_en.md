# Windows (cross-compiled, single sokol exe)

> Since v2.4 the recommended Windows path is the **sokol single exe with no third-party DLLs**.
> The legacy SDL cross-build is kept only as a fallback (see the end).

## 1. Toolchain

```bash
# MSYS2: only a compiler is needed - no SDL packages at all
pacman -S mingw-w64-x86_64-gcc
# or on Linux:
sudo apt install gcc-mingw-w64-x86-64
```
`demo-main/setup_win_cross.sh` prepares the cross toolchain in one step.

## 2. Build

```bash
cd demo-main
./build_win_sokol.sh            # Chinese  -> dist/win_sokol/fxtk_sokol.exe
./build_win_sokol.sh app_en     # English  -> dist/win_sokol/fxtk_sokol_en.exe
```
About **433KB / 430KB**, statically linked (`-static -static-libgcc`, `-mwindows`).

## 3. Verify there are no third-party DLLs

```bash
x86_64-w64-mingw32-objdump -p dist/win_sokol/fxtk_sokol.exe | grep 'DLL Name'
```
Only system DLLs should appear (`KERNEL32`, `USER32`, `GDI32`, `OPENGL32`, `SHELL32`, `ole32`, `dwmapi`, ...) —
no `SDL2.dll`, no `libwinpthread-1.dll`, no `libgcc_s_seh-1.dll`.

## 4. Known Windows limitations (stated honestly)

| Item | Status |
|---|---|
| Image import / PNG screenshot | works (stb codecs, no external deps) |
| Keyboard / mouse / wheel | works |
| GPU warp / two-tier AA / raymarch | works (OpenGL) |
| **Clipboard** | implemented (`OpenClipboard` + `CF_UNICODETEXT`, UTF-8↔UTF-16, CJK-safe) |
| **CJK input method** | **not supported** (XIM is X11-only; Windows would need IMM32) |
| CJK text *display* | works (built-in font, unrelated to input methods) |

If you need clipboard or an IME on Windows, the legacy SDL cross-build still supports both natively:

```bash
./build_win_cross.sh    # legacy: needs mingw-w64 SDL2 packages; produces an exe that ships SDL2 dlls
```
