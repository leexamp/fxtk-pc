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

**Widget pool**: static array `s_pool[FX_MAX_WIDGETS]`, `FX_MAX_WIDGETS = 4096` (about 940KB BSS). `fxtk_alloc()` linearly finds an empty slot, `fxtk_free()` marks it `FX_W_NONE`.

## Layout & Scaling

- Baseline 480×272; `s_sx1000 = width*1000/480`, `s_sy1000` likewise.
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

- Dirty rectangles by default; currently any `fx_repaint_rect` is promoted to a full redraw (`s_full=1`), guaranteeing no ghosting.
- Render into a `target` texture (size = logical width/height); on present, copy the whole thing to the back buffer.
- On resize, rebuild `target` and clear the screen; at end of frame `SDL_RenderSetClipRect(NULL)` prevents clip carry-over.
- Graphics go through **vertex batching**: rectangles/triangles/polylines are merged into a single `SDL_RenderGeometry` submission; `sdl_fill_rect` joins the batch (does not break the draw call).
- Pixel path (offscreen/3D) writes the `fb_rgba` dirty rectangle; `SDL_UpdateTexture` uploads the whole frame in one shot.

## Font & Text Cache

- `fxtk_font_size(size)`: caches `FC_N=8` TTFs; when full, evicts and **closes** the old font (`tc_drop_font` cleans up the associated text textures), eliminating handle leaks.
- Text texture cache has 256 entries, keyed by `font|text|fg|bg`, LRU eviction.

## Input

- The driver reports `touch_read` each frame; `s_last_tx/ty` are **always updated** (usable for hover).
- Press/move/release are dispatched to the top-most hit widget; popups capture with priority.
- Keyboard is enqueued into `g_kq` (64-entry FIFO); the wheel accumulates `g_wheel_pix`.
- Function key mapping: SDL sym → `FX_KEY_*`; Ctrl+Shift state goes into the `mod` bits.

## Popups

Dropdown/right-click menus are a top-level overlay, drawn last and not clipped by the page; clicking outside closes them; near the bottom they auto-pop upward.

## Driver Contract (`fx_driver_t`)

Provides `width/height`, `set_window/push_pixels`, `fill_rect`, `touch_read/key_read`, `blit_img/blit_tex/fill_tri/draw_line/blit_img_rot`, etc.; the core does not depend on any specific platform — swap the driver to port.

## Threads

- `raymarch.c`: POSIX persistent thread pool (`pthread_barrier` two-phase sync each frame, waking workers to render row bands), avoiding create/join every frame; on Windows it falls back to create/join each frame (winpthreads has no barrier).
- `gpu_raymarch.c`: a separate thread does EGL/GLES2 offscreen rendering (rejects llvmpipe software GL); the task queue uses mutex+cond.
