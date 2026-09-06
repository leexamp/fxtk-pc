# fxtk — a tiny, delightful C GUI framework

<p align="center">
  <b>A single-frame, dirty-rect-redraw, attribute-macro-driven GUI for embedded & desktop</b><br>
  <small>Pure C · zero allocations in the hot path · 480×272 responsive design · one header of API</small>
</p>

<p>
  <img alt="version" src="https://img.shields.io/badge/version-v2.3-blue">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
  <img alt="c" src="https://img.shields.io/badge/language-C99-9cf">
  <img alt="platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20ESP32-lightgrey">
</p>

> **English edition.** The Chinese docs live in `README.md` / `docs/`; English sources use an `_en`
> suffix (`docs_en/`, `demo-main/app_en.c`, ...) so both languages sit side by side.

![canvas demo](screenshot_aa.png)
![gradient](screenshot_gradient.png)
![gauge](screenshot_gauge.png)
![checker](screenshot_checker.png)

## Why fxtk?

- **One header, instant clarity** — a widget is a one-liner: `fx_button_new(pixel("10,10","150,40"), title("Go"), call(on_go))`.
- **Immediate-mode canvas + retained widgets** — draw anything pixel-by-pixel, or compose declarative controls.
- **Responsive by default** — author on a 480×272 baseline; the whole UI scales to any window (with a
  sane 1.6× cap so it stays crisp).
- **Cross-platform almost for free** — SDL on PC, `fx_driver_t` abstraction for bare-metal/ESP32.
- **Tiny footprint** — optional widget compilation (`-DFXTK_WIDGET_XXX=0`) plus `tools/autotrim.sh`
  drop unused code out (a hello-world example goes 100KB→88KB).
- **Feature-dense but honest** — AA (scanline), gradient fill, offscreen buffers, GPU raymarching,
  particles, a headless test target, and a CI pipeline. No global singletons you can't reason about.

## What you can build

| Area | What's in the box |
|---|---|
| **Widgets** | button · label · slider · progress · checkbox · grid keypad · canvas · tab · image · textbox · list · dropdown · scroll container |
| **Canvas** | line/circle/ellipse/arc/round-rect/polygon + **anti-aliasing** (`fx_set_aa(1)`, scanline ~60×) + **gradient fill** + offscreen buffer + `anim(1)` |
| **Desktop** | text box (multi-line / click-drag select / Ctrl+A C V X / system clipboard / wheel), drag-scroll, right-click menu |
| **i18n** | `sidebar(FX_TAB_LEFT/RIGHT/TOP/BOTTOM)` puts the tab strip on any edge for long labels; `docs_en/` + `app_en.c` English build (`make fxtk_sim_en`) |
| **Files (v2.3)** | `fx_fs_pick_dir()` (system folder dialog) + `fx_fs_list()` (list a dir → name/size/date) — the demo's Components page is a real file browser with a smooth scrollbar, hover tooltip, and row select |
| **Engine** | sprites · rotation · GPU raymarching (3D) · particles · image post-processing |

## Quick start

```c
#include "fxtk.h"
static void on_go(fx_widget_t *w, void *ud){ (void)w;(void)ud; fx_set_title(w, "Clicked!"); }
void app_init(void){
    fx_set_bg(FX_WINDOW_BG);
    fx_button_new(pixel("10,10","150,40"), title("Go"), color(FX_BTN_BLUE), call(on_go));
}
```

```bash
cd demo-main
make fxtk_sim        # the 12-page demo (Chinese)
make fxtk_sim_en     # the English demo (app_en.c, left-sidebar tabs)
make test            # 9/9 headless assertions, no SDL/window/font
make canvas_08_files # the cross-platform file-browser example
```

Dependencies: **SDL2 / SDL2_ttf / SDL2_image, EGL / GLES2 / X11** (Linux). Windows cross via
`build_win_cross.sh`; the PC simulator only needs SDL. CI (`.github/workflows/ci.yml`) builds + tests
on Linux and cross-compiles for Windows.

## Small binaries (v2.3)

```bash
TRIM=$(../tools/autotrim.sh ../examples/ex01_hello.c)   # -> -DFXTK_WIDGET_GRID=0 ...
gcc -O2 -s $TRIM ... ../examples/ex01_hello.c -o app    # 100KB -> 88KB
```

`tools/autotrim.sh` scans your sources for the widget factories you actually call and emits
`-DFXTK_WIDGET_XXX=0` for every unused type, so unused implementations compile away. Default (no flags)
builds everything.

## Tests

Headless unit tests (`demo-main/test/headless_test.c`) drive the core through a fake `fx_driver_t`
(no SDL/window/font) and verify pixel/percent/grid layout, press/release hit callbacks, value
read/write, widget counting, `fx_find`/`fx_delete`, and the v2.2 canvas API.

## License

MIT.
