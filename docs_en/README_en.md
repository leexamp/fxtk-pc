# fxtk — a tiny C GUI framework where one widget is one line

> **Attribute macros as the UI language.** Each widget factory expands into a plain C99 compound literal —
> no XML, no codegen. The result is a **single binary with zero third-party DLLs**
> (377 KB on Linux / 439 KB on Windows). An **optional Lua 5.4 binding** drives the same widget/canvas/image
> API from scripts (`bindings/lua` + `demo-lua/`, Lua vendored, no system liblua); the C core is unchanged.

<p>
  <img alt="version" src="https://img.shields.io/badge/version-v2.5.4-blue">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
  <img alt="c" src="https://img.shields.io/badge/language-C99-9cf">
  <img alt="platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey">
  <img alt="backend" src="https://img.shields.io/badge/backend-sokol(OpenGL)-brightgreen">
  <img alt="lua" src="https://img.shields.io/badge/Lua-5.4-2C2D72">
</p>

> Verify it yourself: `cd demo-main && make && ldd ./fxtk_sim` — system libraries only,
> no `SDL2.dll`, no `libwinpthread`.
>
> [中文 README](../README.md)

## The syntax: one widget per line

```c
#include "fxtk.h"

/* one callback signature everywhere: void cb(fx_widget_t *w, void *ud) */
static void on_ok(fx_widget_t *w, void *ud) { (void)ud; fx_set_title(w, "clicked"); }
static void on_canvas(fx_widget_t *w, void *ud) { (void)w; (void)ud; /* draw with fx_* here */ }

static void build_ui(void)
{
    fx_label_new   (pixel("16,12","464,40"),   title("Settings"));
    fx_button_new  (pixel("16,56","200,96"),   name("ok"),  title("OK"),     call(on_ok));
    fx_checkbox_new(pixel("16,108","300,140"), name("chk"), title("Enable"), value(1));
    fx_slider_new  (pixel("16,152","380,168"), name("vol"), value(60));
    fx_canvas_new  (pixel("16,184","464,264"), name("cv"),  anim(1), call(on_canvas));   /* immediate-mode canvas */
}
```

`pixel("x1,y1","x2,y2")` uses **design-space coordinates, not device pixels**: the default design
baseline is 480×272 and scales proportionally to any window size — no manual scaling factors. Working from a
1280×720 mockup? Call `fx_set_design_pixel(1280, 720)` at startup and write the mockup coordinates directly.

### Four rules

1. **An attribute is `function(value)`**, any order; a later attribute wins for the same field.
2. **Pick one positioning mode**: `pixel(...)` design coords / `percent(...)` parent-relative /
   `grid(...)` named-grid slices (the last one written wins `pos_mode`).
3. **Callbacks** are always `void cb(fx_widget_t *w, void *ud)`; `fx_find("ok")` gets the widget back later.
4. **Pure C99**: the macro expands to `(fx_attr_t[]){ ..., FX_ATTR_END }`. There is no runtime parser —
   apart from the contents of coordinate strings, everything is checked by the C compiler.

### Attribute cheat sheet

| Kind | Attribute | Notes |
|---|---|---|
| Position | `pixel(...)` `percent(...)` `grid(...)` | choose one |
| Text | `title(s)` / `text(s)` | aliases |
| Size/align | `line(n)` `row(0/1/2)` | `line` = font size or row count depending on widget; `row` = left/center/right |
| Style | `color(bg)` `fgcolor(fg)` `border(n)` `radius(n)` `dense()` | defaults look fine |
| Behaviour | `name(s)` `call(cb)` `value(n)` `page(n)` `maxlen(n)` `anim(n)` | |
| Tab side | `sidebar(FX_TAB_TOP/LEFT/RIGHT/BOTTOM)` | tab widget only |
| Image | `image(img)` | |

> Edge cases (attribute conflicts, deleting widgets inside callbacks, duplicate names, common macro
> compile errors) are written down in **[docs/syntax.md](../docs/syntax.md)** (Chinese).
> Read it before building a real UI.

### Why this style?

- **Shorter than struct init** — no field names, no ordering to remember; one line per widget.
- **More direct than XML/scripting** — no parser, no codegen, no extra build step; the debugger shows
  exactly the code that runs.
- **More information than immediate mode** — widgets stay in a tree: `fx_find`, mutate, lay out again.
- **The trade-off** — coordinates are strings, so a typo is not a compile error. That is why the syntax
  boundaries are documented instead of guessed.

## Lua support: same API, no rewrite

The C core is unchanged. `bindings/lua/fxtk_lua.c` registers the `fxtk.h` API as `fxtk.core`;
`lua/fxtk.lua` provides constructors with the same names as the C attribute macros. Lua 5.4.6 is
vendored in `third_party/lua/` and linked into the executable — no system liblua and no extra DLL.

```lua
require("fxtk").install_globals()

local clicks = 0
local function on_ok(w)
    clicks = clicks + 1
    fx_set_title(fx_find("info"), "clicked " .. clicks)
end

function app_init()
    fx_set_bg(FX_WINDOW_BG)
    fx_label_new(percent("0.02,0.01", "0.98,0.09"), title("Settings"))
    fx_button_new(pixel("16,56", "200,96"), name("ok"), title("OK"), call(on_ok))
    fx_label_new(pixel("16,104", "300,128"), name("info"), title("try me"))
end
```

```bash
make -C demo-main lua        # -> demo-main/fxtk_lua
./demo-main/fxtk_lua         # loads demo-lua/app.lua
make -C demo-main lua-test   # headless binding self-test
cd demo-main && ./build_win_sokol.sh lua   # dist/win_sokol/fxtk_lua.exe
```

Callbacks: `call(function(w) ... end)`; lists: `fx_list_set_cb(list, function(w, idx) ... end)`.
C out-params become multiple Lua returns. Images/SVG are userdata with `__gc`; images attached to
widgets are pinned so GC cannot free them early.

Full 12-page Lua demo: `demo-lua/app.lua`. API mapping, lifecycle, migration and known limits:
**[docs_en/lua_en.md](lua_en.md)**.

## How it looks

Canvas / anti-aliasing / gradients / custom widget (headless v2.2 renders, no font loading — shapes only):

| AA `fx_set_aa(1)` | Gradient `fx_fill_rect_gradient` | Custom widget (gauge) | Checkerboard |
|---|---|---|---|
| ![aa](../screenshot_aa.png) | ![gradient](../screenshot_gradient.png) | ![gauge](../screenshot_gauge.png) | ![checker](../screenshot_checker.png) |

Full 12-page demo (widgets / image warp / pseudo-3D / text input / file browser / particles):

![graph](../graph.png) ![image](../image.png) ![rending](../rending.png) ![texting](../texting.png)

![fxtk demo](../demo.gif)

## Size and dependencies

- **377 KB Linux / 439 KB Windows, single file**; `ldd` / `objdump` show system libraries only.
- Image decoding (PNG/JPEG/BMP/GIF/TGA/PNM), PNG screenshots, clipboard, file dialogs, RNG, time and
  preference storage are **inside the framework** (sokol + stb are vendored as source, not DLLs).
- Size trimming: `-DFXTK_WIDGET_XXX=0` at compile time; `tools/autotrim.sh` scans your sources and
  emits the flags.

## What is in the box

| Area | Contents |
|---|---|
| **Widgets** | button · label · slider · progress · checkbox · grid keypad · canvas · tab · image · textbox · list · dropdown · scroll container · card |
| **Layout** | `pixel` baseline + responsive scaling · `percent` · `grid` · flex (`FX_POS_FLEX`) |
| **Canvas (immediate mode)** | line/circle/ellipse/triangle/polygon/arc/round-rect/text/gradient · offscreen buffer · anti-aliasing · transform stack |
| **Input** | text editing (multi-line, drag-select, Ctrl+A/C/V/X, system clipboard, max length) · wheel routing · animated scroll · scrollbar drag · focus · single-finger gestures |
| **Rendering** | GPU vertex batching · true perspective quad warp · image post-processing · GPU/CPU raymarching · particles |
| **Desktop** | single-window always-on-top/borderless/drag/center · real multi-window (`-DFXTK_MW`, X11/Win32) · in-canvas window manager (`fxtk_wm`) |

## Build and run

```bash
# Linux deps (sokol backend — no SDL2 required)
sudo apt install libx11-dev libxcursor-dev libxi-dev libgl1-mesa-dev

make -C demo-main                 # 12-page demo (fxtk_sim, C)
make -C demo-main lua             # 12-page demo (fxtk_lua, C core + embedded Lua)
make -C demo-main mw_interactive  # multi-window + widget input demo
make -C demo-main test            # headless unit tests (no window, no font)
make -C demo-main lua-test        # Lua binding self-test (headless)
make -C demo-main golden          # canvas golden-image regression (local tolerance 0)
make -C demo-main bench           # headless render benchmark (no vsync)

make -C demo-main ex01_hello      # any standalone example
```

Windows: `demo-main/build_win_sokol.sh` cross-compiles a single exe (CI runs it).
Start your own app from [your_app/](../your_app/) (`main.c` + `build.sh`).

## Verification (including what it does *not* cover)

| Target | Covers | Does not cover |
|---|---|---|
| `make test` | real OS services + deterministic `FXTK_BACKEND_STUB` variant; layout/hit-test/lifecycle/input/colour-geometry contracts; clean under ASan+UBSan | fake driver never rasterises a framebuffer — **no per-pixel assertions** |
| `make golden` | 10 canvas golden images, local tolerance 0 | canvas only; widgets/text/textbox/scroll have **no golden images** |
| `make bench` | 2820 widgets @ 1080p, ~102–130 fps (full-frame redraw) | synthetic scene, not a real app |
| CI | Linux: build + tests + examples + canvas goldens (`GOLDEN_TOL=1`); Windows: cross-compile | Windows is **compile-only**; multi-window is not in CI |

**Not systematically verified**: real HiDPI, multi-monitor, full non-ASCII IME paths, WM differences
(GNOME/KWin/xfwm), extreme window sizes, long-run memory growth. Regression-test in your own environment.

> **AI disclosure**: a large part of this repo is AI-assisted (including the multi-window backend, parts of
> the widget/drawing code and the docs). “It compiles” is not “its edge cases are correct”; the table above
> separates what has evidence from what does not.

## Known limits

- **Full-frame redraw**: dirty-rect merging exists but is currently bypassed (`s_full=1` in
  `fx_repaint_rect`) — no ghosting, but no dirty-rect optimisation either.
- **Widget SDF anti-aliasing** has a known artefact when canvas + text + quad-warp are mixed
  (`fx_set_widget_aa` toggles it).
- **Real multi-window**: X11 only (XWayland works), main screen only, max 8 windows, no clipboard/IME,
  no event callback when the OS moves/resizes a window (since v2.5.3 the backend syncs the client size and
  re-lays out / repaints on the next frame; poll `fxtk_win_width/height`); Win32 and X11 are separate
  implementations.
- **Positioning uses strings**: `pixel("16,56","200,96")` typos are not compile errors
  (there is no `x()/y()/w()/h()` attribute).
- **No i18n layer**: the English demo is a copy (`app_en.c`), not runtime language switching.
- **No** accessibility / RTL / GUI designer; coordinates are `int16`, widget pool is capped at 8192
  (`-DFX_MAX_WIDGETS`).
- **Lua binding is optional and does not change the C core**: callbacks pay a cross-language call;
  the Lua demo caps particles at 4000 and dynamic stress widgets at 1200. The 3D page uses GPU
  raymarching when the host supports it, otherwise a Lua fallback (no CPU raymarch port).
- **ESP32 support was dropped in v2.5.3.** The pure-CPU code paths and `fxtk_backends` stubs remain,
  but CI and `make` targets are gone; `tools/esp32_smoke.sh` and `components/fxtk/CMakeLists.txt` are
  archived only (their source lists are stale — do not use them for ESP-IDF builds).

## Layout

```
components/fxtk/   the framework itself (fxtk.c/draw/widgets/effects/extra/backends + headers)
drivers/           backends (sokol driver + stb text layer + app shell; SDL2 only for headless tests/goldens)
your_app/          start your app here (main.c + build.sh)
demo-main/         12-page demo + toolchain (single Makefile) + headless Lua host
bindings/lua/      Lua binding (registers fxtk.h API as fxtk.core)
lua/               Lua syntax layer (C-macro-shaped constructors + install_globals)
demo-lua/          12-page Lua demo (app.lua) + smoke test (hello.lua)
examples/          standalone examples ex01~ex18 + canvas tutorials
third_party/       vendored sokol + Lua 5.4.6 source (stb lives in components/fxtk/vendor)
docs/ docs_en/     documentation (Chinese / English)
test/golden/       golden reference images
tools/             autotrim / golden / package_release / check_api_docs
```

## Docs

- **[docs/syntax.md](../docs/syntax.md)** — attributes/callbacks/layout priority/naming (**read this first**)
- [quickstart_en.md](quickstart_en.md) · [guide_en.md](guide_en.md) · [api_en.md](api_en.md)
- [desktop_en.md](desktop_en.md) · [effects_en.md](effects_en.md) · [examples_en.md](examples_en.md)
- [internals_en.md](internals_en.md) · [windows_en.md](windows_en.md)
- [multi_window_en.md](multi_window_en.md) · [window_manager_en.md](window_manager_en.md)
- **[lua_en.md](lua_en.md)** — Lua binding: build, API mapping, lifecycle, limits
- Full per-function reference (Chinese): [docs/api.md](../docs/api.md)

## Backends

sokol (OpenGL) is the default and the only actively developed PC backend. SDL2 is kept only for
deterministic headless tests / benchmarks / golden images (`SDL_VIDEODRIVER=dummy`); release artifacts
always use sokol — which is what makes the single-exe Windows build possible.

## License

MIT (examples and docs included). Vendored third-party licenses are in the respective file headers.
