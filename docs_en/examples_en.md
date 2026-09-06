# Examples Guide

| Example | Highlight |
|---|---|
| ex01_hello | Button/label/checkbox, `fx_set_title/fx_set_bg`, light/dark theme switching |
| ex02_widgets | `grid()` keyboard, slider-progress bar linkage, `fx_set_color_w` to change widget color |
| ex03_anim | `anim(1)` canvas, waveform + `fx_fill_polygon_rot` rotated vectors |
| ex04_edit | Input box: selection/clipboard/`maxlen`/wheel, right-click top menu |
| ex05_scroll | Canvas self-drawn long list + `fx_scroll_update` core scrolling + `fx_scrollbar_draw` (dragable thumb) |
| ex06_img | `fx_image_create` + `fx_draw_image_rot` rotated image |
| ex07_snake | `anim(1)` main loop + `fx_last_key` arrow keys + array map/collision/restart |
| ex08_array_buttons | Batch-create buttons in a loop + `name()`/callback source distinction + batch operations |
| ex09_lottery | Random + scroll animation + `fxtk_draw_text_size` large font |
| ex10_extra | List/dropdown/multi-font triple combo `fx_list_new_p`/`fx_drop_add`/`fx_set_fontsize`, popups stay in bounds |
| ex11_percent | `percent()` (0.0~1.0) fill/align, compared with `pixel()` |
| ex12_hover | `fx_touch_state` hover real-time coordinates + hover highlight (updated even when not pressed) |
| ex13_popup | Bottom dropdown auto-pop upward + input box right-click top menu (needs `fxtk_desktop.h`) |
| ex14_resize | Window drag-follow layout, resize without ghosting |
| ex15_textcache | Multi-font mixed layout + font-size slider, verifying font cache eviction doesn't flicker |
| ex16_dynamic | At runtime dynamically create widgets: `fx_widget_fix` fixed coordinates + pointer cache + sine drift |
| ex17_tabs | 3D tabs + `page()` page gates: three pages of different content (button/animation/vector) |
| ex18_defaults | Zero-config default colors: all widgets look good without writing `color()`/`fgcolor()` |

## Canvas learning directory `examples/canvas/` (new in v2.2)

A tutorial series dedicated to learning canvas usage, from basics to advanced, each with detailed comments:

| Example | Highlight |
|---|---|
| canvas_01_primitives | All drawing primitives: line/rect/circle/ellipse/arc/triangle/polygon/round-rect/text |
| canvas_02_immediate | Immediate mode + `anim(1)` per-frame callback: draws a "clock" |
| canvas_03_offscreen | A checkerboard that scales with the window (direct drawing, zoom-safe); also explains the use cases and scaling limits of the offscreen buffer `fx_canvas_set_buf` |
| canvas_04_custom_widget | Self-draw a "custom widget" with the canvas: circular gauge + ring progress (reusable) |
| canvas_05_image | Render images inside the canvas: procedurally generated texture + rotation/scaling/grayscale/tinting |
| canvas_06_interact | Interaction inside the canvas: hold-and-drag a ball (`fx_touch_state`/`fx_pressed`) |
| canvas_07_aa | Anti-aliasing: after `fx_set_aa(1)`, line/circle/rect/round-rect/arc edges are smooth, with a button to toggle ON/OFF in real time |

> v2.2 adds convenient APIs: `fx_canvas_size(w,&cw,&ch)` gets the canvas's local width/height, `fx_canvas_clear(w,color)` one-step clear.
> Run: `./build_ex.sh canvas_01_primitives` or `make -C demo-main canvas_01_primitives`.

### Anti-aliasing (v2.2)

`fx_set_aa(1)` enables edge anti-aliasing: vector primitives such as line/circle/fill_circle/rect/round-rect/arc do edge blending based on "distance to the shape / sub-pixel coverage", making edges smoother (smooth rendering). **After setting `fx_set_aa(1)`, the canvas automatically goes offscreen** to support blending (no manual `fx_canvas_set_buf` needed), see `canvas_07_aa`. `fx_set_aa(0)` turns it off.

### Canvas content must follow window scaling (important)

A `pixel()` canvas scales up/down proportionally with the window, but **inside the canvas the coordinate system is local**. If you hard-code pixel coordinates in the callback,
the content crams into one corner after the window is enlarged (not as expected). The way to handle it is to **write the coordinates/sizes as proportions of `cw`/`ch`**,
and the examples in this directory all do that:

```c
int cw, ch; fx_canvas_size(w, &cw, &ch);
fx_canvas_clear(w, FX_WINDOW_BG);
int px = (int)(cw * 0.5f);                 /* at 50% horizontally */
fx_fill_circle(px, (int)(ch * 0.5f), (int)(cw * 0.05f));
```

### Viewing canvas render results offline (no window)

`demo-main/test/render_canvas.c` uses a fake driver to render the examples to a PPM image, without SDL/window/font,
convenient for checking for breakage/misalignment at different window sizes:

```bash
gcc -O2 -I. -I../components/fxtk test/render_canvas.c ../examples/canvas/canvas_01_primitives.c \
  ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c \
  ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_effects.c -o /tmp/rc -lm
/tmp/rc 800 480 /tmp/frame.ppm       # render at an 800x480 window
convert /tmp/frame.ppm /tmp/frame.png # use ImageMagick to convert to PNG for viewing
```

To run:
```bash
./build_ex.sh              # build and run all examples in one go (including examples/canvas/)
./build_ex.sh all --no-run # only build, don't run
./build_ex.sh ex03_anim    # a single example (replace with any name)
./build_ex.sh canvas_01_primitives  # a single canvas tutorial example
```

## Notes

- **ex13** uses the input-box construction macro `fx_textedit_new`, which is in `fxtk_desktop.h`, so this example additionally `#include "fxtk_desktop.h"`.
- **ex16** demonstrates the three essentials of dynamic widgets: `fx_widget_fix` (prevents layout reset), pointer array (avoids `fx_find`), frame-rate gating (only adds at ≥30fps).
- **ex17**'s canvas uses `anim(1)` animation on page 1; page 2 has no animation (static drawing only the first time / on dirty areas).
- **Default colors**: widgets render as light background with dark text when no color is specified (grid light background with light lines, button blue background with white text); ex18 uses zero explicit colors throughout.
- In **ex12/ex14** the canvas callbacks use `fx_widget_rect` to get the local width/height; coordinates are all in the local system.

## From examples to the full demo

`demo-main/app.c` + `app_desktop.c` is a 12-page comprehensive demo (including 3D ray tracing, GPU particles, dynamic stress test); run `./build.sh` to view it; documentation is in `desktop.md`.
