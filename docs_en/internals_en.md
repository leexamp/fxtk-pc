# fxtk Internals

For people who want to modify the source / port it. For application development, see `guide.md`.

## Module Layout

| File | Responsibility |
|---|---|
| `components/fxtk/fxtk.c` | Widget tree, layout, input dispatch, redraw scheduling, popups, default colors |
| `components/fxtk/fxtk_widgets.c` | Drawing of each widget (button highlight / tab 3D effect / grid, etc.) |
| `components/fxtk/fxtk_extra.c` | Extended widgets such as list / dropdown / scroll |
| `components/fxtk/fxtk_font.c` | TTF font cache + text texture cache |
| `components/fxtk/fxtk_draw.c` | Drawing primitives, `fxtk_text_blit`, clipping |
| `components/fxtk/fxtk_effects.c` | Image effects |
| `demo-main/fxtk_sdl_driver.c` | SDL2 window / render / input / resize |
| `demo-main/raymarch.c` | CPU ray tracing (persistent thread pool + barrier) |
| `demo-main/gpu_raymarch.c` | GPU (EGL/GLES2) ray tracing background thread |

## Object Model

`fx_widget_t` (`fxtk_internal.h`) contains:

- `type/flags/pos_mode`: type / flags (visible, anim, dense, fit, readonly…)/ layout mode
- Layout source: pixel `ox1..oy2`, percent `px1..py2`, grid `gr1..gc2 + grid_ref`
- `title/name/cb/ud/bg/fg/border/radius`
- `lines`(font size) / `rows`(alignment) / `value` / `page`
- Computed rectangle `x1..y2`; parent/child/sibling pointers

**Widget pool**: static array `s_pool[FX_MAX_WIDGETS]`, `FX_MAX_WIDGETS` (**8192 by default ≈ 2.6MB BSS**; `sizeof(fx_widget_t)` = 328B). `fxtk_alloc()` linearly finds an empty slot, `fxtk_free()` marks it `FX_W_NONE`.

> **Memory bill (measured on PC/sokol)**: the binary's `.bss` is about **2.9MB**, of which the widget pool is
> ~2.6MB. The sokol driver's vertex/index/command buffers are **grow-on-demand heap buffers**: initial
> 16k vertices (≈590KB) / 32k indices (≈128KB) / 1k commands; they double only when a frame needs more
> (hard caps remain `VB_MAX=262144` / `IB_MAX` / `CMD_MAX=65536`), and the GPU-side `sg_buffer` is recreated
> only when the capacity grows. Measured demo peaks are just 5k–15k vertices, so most apps never grow.
> The initial capacities can be overridden with `-DVB_INIT=... -DIB_INIT=... -DCMD_INIT=...` (used by tests to
> force the growth path). There is also one **persistent canvas texture** (window-sized RGBA8, ≈3.7MB at
> 1280×720) — dirty-rect redraw relies on it to keep static content across frames.


## Layout & Scaling

- Baseline defaults to 480×272 (variables `fx_design_w/h`, changed by `fx_set_design_pixel()`);
  `s_sx1000 = width*1000/fx_design_w`, `s_sy1000` likewise.
- `FX_POS_PIXEL`: design coordinates × scale; `FX_POS_PERCENT`: parent rectangle × float; `FX_POS_GRID`: named grid.
- **`FX_POS_FIXED`**: layout skipped (`case FX_POS_FIXED: break;`), coordinates written directly. Used by popups and dynamic widgets.
  - `fx_widget_fix(w,x,y)` switches a widget to FIXED and records the base `ox1/oy1`, for later self-draw movement.
- On resize, `sdl_apply_size` updates the scale and does `fx_layout()` + a full redraw.

## Default Colors (fxtk.c `fx_widget_new_impl`)

- Window background `s_bg = FX_RGB(245,245,245)`.
- All widgets' `fg` defaults to `FX_RGB(40,40,40)` dark text; `bg` by type:
  - label/checkbox/image: `FX_BLACK` sentinel → use window background when drawing (transparent)
  - grid/panel/tab/scroll: light background + `FX_LGRAY`
  - slider/progress: green fill + light gray track
  - textedit: `FX_BLACK` sentinel → white background with black text
  - button/default: blue background `FX_RGB(33,150,243)` + white text

> `FX_BLACK` in `bg` is an "unspecified" sentinel (label transparent, textedit white background); to explicitly use a black background use `FX_RGB(0,0,0)`.

## Redraw Model

- **Dirty-rect partial redraw (restored in v2.5.6)**: `fx_repaint_rect` merges the rectangle into `s_dirty[]`
  (≤8 regions, overlaps merged, overflow falls back to a full frame). Before choosing the draw branch,
  `fx_poll()` marks every `FX_F_ANIM` widget dirty each frame (canvas callbacks advance phase per call —
  skipping one frame diverges permanently from full redraw), then repaints region by region with the tree
  clipped to each region.
- **Two traps (fixed, do not reintroduce)**: ① when a tab draws its page it must intersect its child clip
  with the incoming dirty region instead of resetting it to the whole tab rect (otherwise widgets outside
  the region are repainted without background erase and AA text darkens every frame); ② snapshot and clear
  the dirty list before consuming it — new marks raised by callbacks during repaint belong to the next frame,
  otherwise the same canvas is painted several times in one frame and per-call phase runs too fast.
- `FXTK_FULL_REDRAW=1` restores "any request repaints the whole frame"; correctness is guarded by a
  370-frame full-vs-dirty pixel A/B over 5 scenarios.
- sokol backend: draws into a **persistent canvas texture** (`LOAD` keeps last frame's static content, dirty
  frames repaint only the changed blocks); commands merge by pipeline + texture + **clip rect** (different
  clips never merge, otherwise the scissor clips the geometry away), then the texture is blitted to the swapchain.
- SDL fake driver (headless benchmarks/goldens): renders into a `target` texture and copies it on present;
  the pixel path writes `fb_rgba` dirty rectangles and uploads once.

## Font & Text Cache

- **Logical font size → device pixels**: `size` is a logical size; both backends use
  `device_size = size × fx_dpi_scale()` for font selection, measurement and drawing (on HiDPI the physical
  framebuffer doubles, so the font must double too; at `dpi=1` output is byte-identical).
- SDL/ttf backend (`components/fxtk/fxtk_font.c`): caches `FC_N=8` TTFs; when full, evicts and **closes** the
  old font (`tc_drop_font` cleans up the associated text textures), eliminating handle leaks.
- sokol/stb backend (`drivers/fxtk_font_stb.c`): `FONT_MAX=8` size slots, each with a **1024-entry advance
  direct-map cache** (`tag = cp+1`, avoids the per-glyph cmap binary search when measuring); 128 text textures cached.
- Text texture cache has 256 entries, keyed by `font|text|fg|bg`, LRU eviction.

## Input

- The driver reports `touch_read` each frame; `s_last_tx/ty` are **always updated** (usable for hover).
- Press/move/release are dispatched to the top-most hit widget; popups capture with priority.
- Keyboard is enqueued into `g_kq` (64-entry FIFO); the wheel accumulates `g_wheel_pix`.
- Function key mapping: SDL sym → `FX_KEY_*`; Ctrl+Shift state goes into the `mod` bits.
- **Unified scrolling (v2.5.5)**: `fx_poll` routes wheel deltas to the nearest `FX_W_SCROLL/FX_W_TEXTEDIT/FX_W_CANVAS`
  under the cursor (canvases keep the old contract; callbacks may call `fx_wheel_take` themselves for zoom).
  Each widget calls `fxtk_scroll_step()` from its draw path to consume the delta and advance easing. Touch drag
  uses an `s_touch_sc` candidate plus an 8px slop before taking over; release velocity is stored in `vel`
  for momentum. The state pool `s_scroll_pool[FX_MAX_SCROLL_STATES]` is indexed by widget pointer; when full it
  degrades to direct (non-eased) scrolling with a LOG_WARN instead of aliasing two widgets.
- **Touchscreen detection**: `fx_touch_capable()` detects and caches on first use (Linux reads
  `INPUT_PROP_DIRECT` from `/sys/class/input/*/device/properties`; touchpads are `INPUT_PROP_POINTER` and do not
  count; Windows uses `GetSystemMetrics(SM_MAXIMUMTOUCHES)`). `fx_set_touch_mode()` / `FXTK_TOUCH` override it.
  Without a detected touchscreen, drag-to-scroll is not enabled at all.

## Popups

Dropdown/right-click menus are a top-level overlay, drawn last and not clipped by the page; clicking outside closes them; near the bottom they auto-pop upward.

## Driver Contract (`fx_driver_t`)

Provides `width/height`, `set_window/push_pixels`, `fill_rect`, `touch_read/key_read`, `blit_img/blit_tex/fill_tri/draw_line/blit_img_rot`, etc.; the core does not depend on any specific platform — swap the driver to port.

## Threads

- `raymarch.c`: POSIX persistent thread pool (`pthread_barrier` two-phase sync each frame, waking workers to render row bands), avoiding create/join every frame; on Windows it falls back to create/join each frame (winpthreads has no barrier).
- `gpu_raymarch.c`: a separate thread does EGL/GLES2 offscreen rendering (rejects llvmpipe software GL); the task queue uses mutex+cond.

## v2.5 driver internals

- `FXTK_MW` branch: per-size canvas/framebuffer caches (24 slots), `MW_NEED_CLEAR`, alpha-preserving
  `rgba_pack`; `CMD_MAX` raised to 65536. Since v2.5.6 the vertex/index/command buffers grow on demand
  (initial 16k/32k/1k) up to those hard caps. Multi-window window/event handling lives in
  `drivers/fxtk_sokol_mw.c`. Details: [docs/multi-window.md](../docs/multi-window.md).

