# Lua binding (fxtk + Lua 5.4)

> **Core: fxtk-pc-v2.5 (C99, `FXTK_VERSION` = 2.5.5, semantics unchanged).**
> Lua is a binding layer, not a rewrite: widgets, canvas, images, input and backend services
> are still implemented in C. This repository adds Lua 5.4 access to the same API and a
> 12-page Lua demo.

- Binding: `bindings/lua/fxtk_lua.c` / `fxtk_lua.h`
- Syntax layer: `lua/fxtk.lua` (C attribute-macro-shaped constructors + `install_globals()`)
- Lua 5.4.6: `third_party/lua/` (vendored source, linked into the executable — no system liblua)
- Demo: `demo-lua/app.lua` (12 pages); `demo-lua/hello.lua` (smoke test)
- Hosts: `drivers/main_lua.c` (sokol window), `demo-main/test/lua_host_sdl.c` (headless SDL dummy)

## Build & run

```bash
make -C demo-main lua        # -> demo-main/fxtk_lua_runner (+ fxtk_lua compatibility symlink)
cd demo-main
./build.sh --lua             # or build+run in one step
./fxtk_lua_runner            # runs ./app.lua (falls back to demo-lua/app.lua)
./fxtk_lua_runner my_app.lua # explicit script
./fxtk_lua_runner my_app/    # directory -> my_app/app.lua
FXTK_SCRIPT=../demo-lua/hello.lua ./fxtk_lua_runner
FXTK_PAGE=3 ./fxtk_lua_runner # start on page 3 (0-based)
FXTK_CLICKS='281,84,10;100,330,20' ./fxtk_lua_runner     # inject clicks
FXTK_DRAG='250,300,650,450,12,20' ./fxtk_lua_runner       # x0,y0,x1,y1,steps,frame

make -C demo-main lua-sdl    # -> demo-main/test/lua_host_sdl
SDL_VIDEODRIVER=dummy ./test/lua_host_sdl ../demo-lua/app.lua 30 3 /tmp/lua_p3.png
# args: [script] [frames] [page] [screenshot]; env fallbacks:
# FXTK_SCRIPT / FXTK_FRAMES / FXTK_PAGE / FXTK_SHOT / FXTK_WIN_W / FXTK_WIN_H

make -C demo-main lua-test   # headless binding self-test
# expected: [lua-test] OK: attrs/widgets/callbacks/canvas/image/list/flex/svg/file/gesture

cd demo-main && ./build_win_sokol.sh lua   # fxtk_lua.exe + fxtk_lua_runner.exe (Lua embedded)
```

> **Scripts are not compiled.** The runner reads `app.lua` at startup; edit the script and rerun, no rebuild.
> Only C binding/core changes need `make lua`. `fxtk_lua` is a compatibility name for `fxtk_lua_runner`.

Script lookup order: CLI argument -> `FXTK_SCRIPT` -> `./app.lua` -> `./demo-lua/app.lua` ->
`../demo-lua/app.lua` -> `<exe>/app.lua` -> `<exe>/demo-lua/app.lua`. A directory argument means
`<dir>/app.lua`.

## Host contract

```lua
function app_init()            -- called once after fx_init
end

function app_frame(dt, frame)  -- optional; called every frame before fx_poll
end
```

The host owns `fx_poll()`; do not call it from the script. Lua errors inside callbacks are
caught with `lua_pcall` + traceback, logged via `fx_log(FX_LOG_ERROR, ...)`, and do not crash C.

## Syntax: same as C attribute macros

```lua
require("fxtk").install_globals()

fx_label_new(pixel("16,12", "464,40"), title("Settings"))
fx_button_new(pixel("16,56", "200,96"), name("ok"), title("OK"),
              color(FX_BTN_BLUE), call(function(w)
                  fx_set_title(w, "clicked")
              end))
```

| C | Lua |
|---|---|
| `pixel("x,y","x2,y2")`, `percent(...)`, `grid(...)` | same names/arguments |
| `title`, `text`, `name`, `call`, `page`, `anim`, `dense`, `value` | same |
| `color`, `fgcolor`, `border`, `radius`, `line`, `row`, `sidebar`, `maxlen`, `image` | same |
| `fx_button_new(...)` etc. | same C function names |
| `fx_widget_rect(w,&x1,&y1,&x2,&y2)` | `local x1,y1,x2,y2 = fx_widget_rect(w)` |
| `fx_canvas_size(w,&w,&h)` | `local w,h = fx_canvas_size(w)` |
| `fx_touch_state(&x,&y,&p)` | `local x,y,p = fx_touch_state()` |
| `fx_last_key(&k)` | `local k = fx_last_key()` (`k.key`, `k.utf8`) |
| `fx_scroll_offset/max/view_h(w)` | same names, return integers; `fx_scroll_update(w,h)` returns the offset |
| `fx_touch_capable()` / `fx_set_touch_mode(n)` | same names; the first returns a boolean (n: -1 auto / 0 off / 1 on) |

Attributes are Lua functions returning a table tagged with `__fx`. Widget constructors accept
varargs of attributes. Module style is also available:

```lua
local fx = require("fxtk")
fx.core.fx_label_new(fx.pixel("16,12", "200,40"), fx.title("hi"))
```

## Callbacks

- Normal widgets: `call(function(w) ... end)` or `fx_set_cb(w, fn)`.
- Lists: `fx_list_set_cb(list, function(w, idx) ... end)`, `idx` is 0-based.
  Do **not** call `fx_set_cb` on a list/dropdown; the C core uses it for its internal callback.
- The C `ud` argument of `fx_set_cb` is not exposed; Lua stores functions in the `fxtk.cbs`
  registry keyed by widget pointer.
- `fx_delete(w)` accepts a handle, a name string, or a grid/attr table and clears the Lua
  callback/image registrations for that widget. It does not recurse into children.

## Images / SVG

```lua
local img = fx_image_create(120, 90)   -- full userdata with __gc
fx_image_fill(img, FX_WHITE)
fx_image_line(img, 0, 0, 119, 89, FX_RED, 2)
local w, h = fx_image_size(img)
local c = fx_image_get_px(img, 10, 10)

local svg = fx_svg_parse("<svg ...>...</svg>")
fx_svg_draw(svg, 10, 10, 100, 100)
```

Images attached with the `image(img)` attribute or `fx_set_image(w, img)` are pinned in
`fxtk.imgrefs` so Lua GC cannot free them while the widget is alive; deleting the widget
releases the pin.

## Demo pages

0 wave, 1 graphics/pseudo-3D, 2 controls, 3 images/quad warp, 4 3D raymarch, 5 text input,
6 paint, 7 keyboard/mouse monitor, 8 stress, 9 scrolling canvas, 10 components, 11 particles.
Lua caps particles at 4000 and dynamic stress widgets at 1200 to keep cross-language overhead
bounded.

## Verification

- `make lua-test`: attrs/widgets/callbacks (including list index)/canvas/image/list/flex/SVG/
  file/gesture; returns non-zero on failure.
- Screenshot parity (1280x720, SDL dummy, 30 frames) against the C demo: pages 2, 3, 5, 6, 9, 10
  differ by 1 pixel; animated pages 0/1 differ by ~1000 pixels (frame phase); pages 7/8/11 are dynamic.
  This is evidence, not a CI pixel gate. Details in `docs/lua.md`.

## Known limits

- Not a Lua rewrite of the C core; hot paths pay a cross-language call per API call.
- No CPU raymarch port for Lua: page 4 uses GPU raymarch when the host supports it, otherwise a
  Lua fallback animation.
- Headless SDL host compiles with `FXTK_LUA_NO_WINAPI`; `fx_win_*` are stubs there. The sokol
  host links `drivers/fxtk_window.c` (Win32/X11 implementations).
- Not covered: Lua coroutines mixed with draw callbacks, hot reload, full IME chain, multiple Lua states.
- Not bound: `fx_wm_*` (in-canvas window manager), `fx_par_for` / `fx_parallel_for` (C-thread
  callbacks, unsafe across a Lua state), the C CPU raymarch implementation, legacy `fx_fs_*`;
  `fxtk_font_height` is declared but has no backend implementation. `fx_init` / `fx_poll` are host-owned.
- Windows runtime: only cross-compilation and DLL dependency checks were verified here; the full
  interaction was not run on a real Windows machine (sokol/GL is not usable under wine here).
