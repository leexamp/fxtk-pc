# fxtk Complete Guide (guide.md)

> This guide covers all of fxtk's common capabilities: from the first window to custom-drawn animation, data widgets, and performance tuning.
> Environment: pure PC / Linux simulator (`demo-main/`), SDL2 rendering, fully decoupled from ESP32.
> Suggested order: Chapters 1–4 → get it running with quickstart → Chapters 5–9 for writing real pages → Chapters 10–14 for deeper topics.

---

## Table of Contents

1. [Overall Model](#1-overall-model)
2. [Building and Running](#2-building-and-running)
3. [Coordinate System and Layout](#3-coordinate-system-and-layout)
4. [Color System](#4-color-system)
5. [Widget Creation (Attribute Macros)](#5-widget-creation-attribute-macros)
6. [Widget Types in Detail](#6-widget-types-in-detail)
7. [Containers and Pages](#7-containers-and-pages)
8. [Drawing Primitives (Immediate-Mode Canvas Drawing)](#8-drawing-primitives-immediate-mode-canvas-drawing)
9. [Input: Touch/Keyboard/Wheel](#9-input-touch-keyboardwheel)
10. [Runtime Widget Operations](#10-runtime-widget-operations)
11. [Images and Effects](#11-images-and-effects)
12. [Text and Fonts](#12-text-and-fonts)
13. [Popups and Context Menus](#13-popups-and-context-menus)
14. [Repaint and Performance Model](#14-repaint-and-performance-model)
15. [Complete Example: A Waveform Page from Scratch](#15-complete-example-a-waveform-page-from-scratch)
16. [Common Pitfalls and Best Practices](#16-common-pitfalls-and-best-practices)

---

## 1. Overall Model

fxtk is a lightweight GUI that mixes **"retained-mode widgets + immediate-mode drawing"**:

- **Widgets are retained-mode**: create once, they live for a long time, and the core handles layout, drawing, and input dispatch uniformly. Change the text, change the rectangle, change the color — the core redraws automatically.
- **Canvas content is immediate-mode**: attach a callback to a canvas, and the core calls it on demand; inside the callback you "draw" directly with drawing primitives — no scene graph, no state synchronization, whatever you draw is what appears.
- **Single-threaded + driver abstraction**: the core does not depend on an operating system; it only depends on a display/input driver (`fx_sdl_driver`). Swap the driver to port to another platform.

Understanding this model only requires remembering one thing:

> **Widgets manage "what there is"; canvas manages "what to draw."**

### 1.1 A Minimal Application

Writing an application only requires implementing `app_init()` — the main loop, window, and driver are all provided by `demo-main/main_linux.c` (the examples use the same structure):

```c
/* Minimal app: after compiling it shows label+button+animated canvas */
#include "fxtk.h"
#include <stdio.h>

static void on_btn(fx_widget_t *w, void *ud) {
    static int n = 0;
    char b[32]; snprintf(b, sizeof(b), "clicked %d times", ++n);
    fx_set_title(w, b);                  /* change title, auto repaint */
}

static void on_draw(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1);   /* clear background */
    fx_set_color(FX_YELLOW); fx_draw_circle(cw / 2, ch / 2, ch / 3);
}

void app_init(void) {
    fx_label_new(pixel("10,10", "300,34"), title("First App"));
    fx_button_new(pixel("10,40", "200,80"), title("Click Me"), call(on_btn));
    fx_canvas_new(pixel("10,90", "470,262"), anim(1), call(on_draw));
}
```

Build and run: `./build_ex.sh <your file>` (put the file into `examples/`), or write your own main by referring to `main_linux.c`.

---

## 2. Building and Running

### 2.1 Complete Demo

```bash
cd demo-main
./build.sh          # compile & run fxtk_sim (the full 12-page demo)
```

### 2.2 Standalone Examples

```bash
./build_ex.sh              # build every example in examples/
./build_ex.sh all --no-run # build all only, skip running
./build_ex.sh ex03_anim    # build & run a single example
```

### 2.3 Environment Variables

| Variable | Effect |
|---|---|
| `FXTK_FONT=/path/to.ttf` | Specify the font (default searches the built-in fallback list) |
| `FXTK_MAXW=1920` | Limit the maximum logical width (scaling cap) |
| `FXTK_STAT=1` | Print GPU vertex count/frame time statistics every second |
| `FXTK_COMPACT=1` | No UI scaling cap on large screens (full ratio) |
| `FXTK_BENCH=1` | Disable vsync to measure the real render frame rate |

### 2.4 Windows

See `docs/windows.md`: WSL2+WSLg zero changes / MSYS2 native exe / MSVC+vcpkg.

### 2.5 Optional Widget Compilation (v2.3, reduce size)

By default, all widgets are compiled in; for size-constrained platforms such as ESP32, you can **cut out the widgets you don't use on demand** to reduce the binary:

```bash
# drop buttons+checkbox in an example: all-on 170KB -> 142KB
gcc -O2 -s -pthread -DFXTK_WIDGET_BUTTON=0 -DFXTK_WIDGET_CHECKBOX=0 ... -o app
```

Cuttable widgets (default 1=on, set 0=off): `FXTK_WIDGET_BUTTON/LABEL/GRID/CANVAS/SLIDER/PROGRESS/CHECKBOX/PANEL/TAB/IMAGE/TEXTEDIT/LIST/DROP`.

- Disabled widgets **can still be created** (the type enum is unchanged and the create macro is still there); they are just **no longer drawn** — so only cut out widgets you truly don't use, and actually run it once to confirm.
- `FXTK_WIDGET_CANVAS`/`TEXTEDIT`/`IMAGE` are basic/extended; don't disable them lightly; once `CANVAS` is disabled the whole framework is basically unusable.
- The PC demo keeps everything on by default, behavior unchanged.

---

## 3. Coordinate System and Layout

### 3.1 Design Baseline

- The design baseline is **480×272** (`FX_DESIGN_W/H`).
- The core computes `s_sx1000 = width*1000/480` and `s_sy1000 = height*1000/272` from the actual window size, and all `pixel()` coordinates are **proportionally scaled** by these.
- Therefore the UI **fills the screen and keeps the same proportions at any resolution**: a 480×272 window displays at original size, and 1920×1080 fullscreen auto-magnifies by 4×.

### 3.2 Three Layout Modes

| Mode | Attribute macro | Description |
|---|---|---|
| Pixel | `pixel("x1,y1","x2,y2")` | 480×272 design coordinates, proportionally scaled. **Most commonly used** |
| Percent | `percent("a,b","c,d")` | 0.0~1.0 floating-point strings, relative to the parent widget rectangle |
| Grid | `grid("name",r1,c1,r2,c2)` | Hangs in the cell range of a named grid; `grid("name")` fills it |

```c
/* pixel: fixed position */
fx_label_new(pixel("6,36", "240,56"), title("Top-left"));
/* percent: fill the parent container */
fx_canvas_new(percent("0,0", "1,1"), name("full"));
/* grid: keyboard keys */
fx_grid_map(pixel("6,32", "280,220"), line(3), row(3), name("keys"));
fx_button_new(grid("keys", 2, 2, 2, 2), title("5"), call(on_key));
```

### 3.3 Scaling Cap (large-screen protection)

By default, widgets are maximally magnified 1.6× (`fx_set_max_scale(1.6f)`) so widgets do not become huge on large screens.
If custom-drawn content needs to follow the UI scaling:

```c
int rh = 20 * fxtk_ui_scale() / 100;   /* fxtk_ui_scale(): 480 wide=100 */
if (rh < 12) rh = 12;
```

---

## 4. Color System

Colors are **24bit RGB** (`uint32_t`, format `0xRRGGBB`), matching mainstream standards.

### 4.1 Construction and Constants

```c
fx_set_color(FX_RGB(33, 150, 243));     /* any 8-bit component */
```

| Constant | Value | Constant | Value |
|---|---|---|---|
| `FX_BLACK` | `0x000000` | `FX_WHITE` | `0xFFFFFF` |
| `FX_RED` | `0xFF0000` | `FX_GREEN` | `0x00FF00` |
| `FX_BLUE` | `0x0000FF` | `FX_YELLOW` | `0xFFFF00` |
| `FX_CYAN` | `0x00FFFF` | `FX_MAGENTA` | `0xFF00FF` |
| `FX_GRAY` | `0x848484` | `FX_LGRAY` | `0xC8C8C8` |

### 4.2 Default Palette (important!)

**Widgets look fine even without specifying a color**; the default is "light background, dark text":

| Widget | Default background | Default foreground |
|---|---|---|
| window | `FX_RGB(245,245,245)` | — |
| label / checkbox | transparent (window color) | `FX_RGB(40,40,40)` dark text |
| button | `FX_RGB(33,150,243)` blue | `FX_WHITE` white text |
| grid / panel / tab | `FX_RGB(240,240,240)` | `FX_LGRAY` grid lines |
| slider / progress | `FX_RGB(76,175,80)` green | `FX_LGRAY` track |
| canvas | `FX_RGB(240,240,240)` | — |
| textedit | white background | black text |

Therefore:

```c
/* Looks good even without any color()/fgcolor() */
fx_label_new(pixel("10,10","300,34"), title("Default dark on light"));
fx_button_new(pixel("10,40","200,80"), title("Default blue bg white text"));
fx_grid_map(pixel("10,80","300,220"), line(3), row(3));   /* light bg light grid lines */
```

Specify explicitly only when you need to change the color:

```c
fx_button_new(pixel("10,40","200,80"), title("Red"),
              color(FX_RGB(244,67,54)));                /* red bg white text */
fx_label_new(pixel("10,10","300,34"), title("Gray annotation"),
             fgcolor(FX_RGB(120,120,120)));
```

Change the window background: `fx_set_bg(FX_RGB(245,245,245))`; read the current background: `fx_get_bg()`.

---

## 5. Widget Creation (Attribute Macros)

All `_new` functions follow a **variadic attribute-list** style: attributes are built with macros, and the order is arbitrary.

```c
fx_widget_t *fx_label_new(pixel("10,10","300,34"), page(3),
                          line(14), row(1), title("Hello"),
                          fgcolor(FX_RGB(51,51,51)), call(on_x));
```

### 5.1 Common Attribute Macros (available on all widgets)

| Macro | Effect |
|---|---|
| `pixel(a,b)` / `percent(a,b)` / `grid(...)` | layout rectangle (see Chapter 3) |
| `title("...")` | title/text |
| `name("...")` | widget name (for lookup with `fx_find`, **must be unique**) |
| `call(fn)` | callback `void fn(fx_widget_t *w, void *ud)` |
| `color(c)` | background color |
| `fgcolor(c)` | foreground color (text/track color) |
| `line(n)` | font size (label default 18; button/label both work) |
| `row(n)` | alignment: 0 left / 1 center / 2 right |
| `value(n)` | initial value 0~100 (slider/progress/checkbox, etc.) |
| `page(n)` | hang onto page n of a tab |
| `anim(1)` | canvas redraws every frame (animation) |
| `border(n)` / `radius(n)` | border width / corner radius |
| `dense()` | grid dense mode (no cell padding) |

### 5.2 Callbacks and User Data

```c
static void on_key(fx_widget_t *w, void *ud) {
    int id = (int)(intptr_t)ud;          /* get the argument from ud */
    ...
}
/* pass arguments at creation */
fx_set_cb(w, on_key, (void *)(intptr_t)3);     /* or set after creation */
```

---

## 6. Widget Types in Detail

### 6.1 `fx_label_new(...)` — Text Label

`line()` sets the font size, `row()` sets alignment (0/1/2), `fgcolor()` sets the text color.

```c
fx_label_new(pixel("8,30","472,50"), line(15), row(1),
             title("Centered title"), fgcolor(FX_RGB(40,40,40)));
```

At runtime: `fx_set_title(w,"new text")`, `fx_set_fontsize(w,20)`, `fx_set_align(w,1)`.

### 6.2 `fx_button_new(...)` — Button

`call()` fires on **click release**. It has built-in pressed state and highlight/shadow 3D effects.

```c
fx_button_new(pixel("340,40","400,70"), name("br_r"), page(6),
              title("Red"), color(FX_RGB(244,67,54)), call(on_brush));
```

### 6.3 `fx_canvas_new(...)` — Canvas (most flexible)

- `call()` is a **draw callback** (immediate-mode).
- `anim(1)`: redraw every frame (animation); otherwise only dirty region/first draw.
- Coordinates inside the callback are **widget-local** (0,0 is the top-left); get width/height with `fx_canvas_size(w,&cw,&ch)` (or `fx_widget_rect`).
- Clear the background with `fx_canvas_clear(w,color)` (replaces hand-written `fx_set_color`+`fx_fill_rect`).
- A canvas can act as a **container**: child widgets created after `fx_parent(canvas)` are drawn on top of the canvas content (C2 pass), and are clipped with the canvas.

```c
fx_canvas_new(pixel("6,32","444,236"), name("wave_cv"), page(0),
              anim(1), color(FX_RGB(30,30,30)), call(on_wave));
```

### 6.4 `fx_slider_new(...)` — Slider

`value()` is the initial value; `call()` fires continuously while dragging; `fx_get_value(w)` reads the current value 0~100.

```c
static void on_speed(fx_widget_t *w, void *ud) {
    int v = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), v);    /* link the progress bar */
}
fx_slider_new(pixel("60,194","292,212"), name("speed"), value(30), call(on_speed));
```

### 6.5 `fx_progress_new(...)` — Progress Bar

Read-only display, `value()` 0~100, updated with `fx_set_value`.

```c
fx_progress_new(pixel("300,196","444,210"), name("speed_bar"), value(30));
```

### 6.6 `fx_checkbox_new(...)` — Checkbox

`title()` is the text on the right; clicking toggles `value` (0/1); `call()` fires the callback.

```c
fx_checkbox_new(pixel("6,218","150,240"), name("wave"), title("Wave switch"),
                value(1), call(on_wave));
```

### 6.7 `fx_textedit_new(...)` — Input Box (desktop extension, requires `fxtk_desktop.h`)

- Keyboard input, Backspace, arrow keys, Home/End.
- **Text selection**: Shift+arrow keys / Shift+click-drag.
- **System clipboard**: Ctrl+A select all, Ctrl+C copy, Ctrl+V paste, Ctrl+X cut.
- `maxlen(n)`: limit length and show an `n/max` counter.
- Mouse wheel scrolls when content is very long. Right-click an input box pops up an on-top menu (copy/paste/select all).

```c
fx_textedit_new(pixel("6,60","444,140"), name("edit1"), title("hello world"));
fx_textedit_new(pixel("6,148","444,182"), name("edit2"), title(""), maxlen(20));
```

### 6.8 `fx_list_new_p("r1","r2",pg)` — List

- `pg` is the **tab page index** the widget belongs to (`-1` means it does not belong to any tab); mouse wheel scrolls; click to select and highlight.
- Data: `fx_list_add(w, text)`; selection callback: `fx_list_set_cb(w, fn)`.
- In the callback, `fx_list_sel(w)` reads the selected row index.

```c
fx_widget_t *list = fx_list_new_p("8,106","200,236", 3);   /* put into the 3rd tab */
for (int i = 0; i < 12; i++) fx_list_add(list, names[i]);
fx_list_set_cb(list, on_nc_pick);
```

### 6.9 `fx_drop_new_p("r1","r2",pg)` — Dropdown Box

- `pg` is the **tab page index** the widget belongs to (`-1` means it does not belong to any tab).
- `fx_drop_add` adds options; `fx_list_set_cb` sets the selection callback.
- Near the bottom it **automatically pops upward**; when the popup content overflows it scrolls and never crosses the edge; clicking outside auto-collapses it.

```c
fx_widget_t *drop = fx_drop_new_p("220,106","360,132", 3);
fx_drop_add(drop, "Font: Small");
fx_drop_add(drop, "Font: Medium");
fx_drop_add(drop, "Font: Large");
fx_list_set_cb(drop, on_nc_drop);
```

### 6.10 Others

| Widget | Description |
|---|---|
| `fx_panel_new(...)` | Panel container (light background + dark border) |
| `fx_tab_new(...)` | Tab container (see Chapter 7) |
| `fx_grid_map(...)` | Grid container (background + grid lines) |
| `fx_image_new(...)` | Image widget (see Chapter 11) |

---

## 7. Containers and Pages

### 7.1 Parent-Child Relationship

When creating a widget, it hangs under the "current parent widget":

```c
fx_parent(fx_find("tab"));       /* widgets created afterwards hang under tab */
... create a batch of widgets ...
fx_parent(NULL);                 /* back to root */
```

### 7.2 Tabs and Pages

```c
fx_tab_new(pixel("10,26","470,266"), title("Waveform, Graphics, Controls, Images"),
           name("tab"), color(FX_RGB(224,224,224)));
fx_parent(fx_find("tab"));
fx_canvas_new(pixel("6,32","444,196"), name("wave_cv"), page(0), anim(1), call(on_wave));
fx_button_new(pixel("6,202","76,236"), page(1), title("Switch mode"), call(on_mode));
fx_parent(NULL);
```

- `page(n)` decides which page n the widget appears on; the tab's `value` (current page) is switched by clicking a tab.
- The selected tab has a **3D raised** look (bright background, dark text + highlight); unselected tabs are recessed.
- Tab titles are page names separated by commas.
- **Sidebar orientation `sidebar(side)` (v2.3)**: put the tab strip on any of the four edges — top/left/right/bottom — left/right edges are common when English labels are long (`FX_TAB_SIDE=88` wide); top/bottom use `FX_TAB_H=24` high.

```c
fx_tab_new(pixel("6,24","438,236"), title("Home, Settings, About"), sidebar(FX_TAB_LEFT));
/* or FX_TAB_TOP (default) / FX_TAB_RIGHT / FX_TAB_BOTTOM */
```

### 7.3 Canvas as Container

```c
fx_canvas_new(pixel("6,32","444,196"), name("gfx_cv"), page(1), anim(1), call(on_gfx));
fx_parent(fx_find("gfx_cv"));     /* child widgets are drawn above the canvas content */
fx_button_new(pixel("20,40","90,64"), page(1), title("Embedded"), call(on_mode));
fx_parent(fx_find("tab"));        /* restore parent */
```

---

## 8. Drawing Primitives (Immediate-Mode Canvas Drawing)

All coordinates are **widget-local** (0,0 is the widget's top-left).

### 8.1 Basic Shapes

| Function | Description |
|---|---|
| `fx_set_color(c)` | set the current drawing color |
| `fx_draw_pixel(x,y)` | single pixel |
| `fx_fill_rect(x1,y1,x2,y2)` | filled rectangle |
| `fx_draw_rect(x1,y1,x2,y2)` | rectangle border |
| `fx_draw_hline(x1,x2,y)` | horizontal line |
| `fx_draw_vline(x,y1,y2)` | vertical line |
| `fx_draw_line(x1,y1,x2,y2)` | arbitrary line |
| `fx_draw_circle(cx,cy,r)` / `fx_fill_circle` | circle |
| `fx_draw_ellipse(cx,cy,rx,ry)` / `fx_fill_ellipse` | ellipse |
| `fx_draw_arc(cx,cy,r,a1,a2)` / `fx_fill_arc` | circular arc (degrees) |
| `fx_draw_rect_round(...)` / `fx_fill_rect_round(...)` | rounded rectangle |
| `fx_fill_rect_gradient(x1,y1,x2,y2,c1,c2,vertical)` | gradient fill: `c1→c2` linear transition (`vertical=1` top-bottom, `0` left-right) |

```c
fx_fill_rect_gradient(20, 20, 240, 120, FX_BTN_BLUE, FX_OK_GREEN, 1);   /* blue→green top-bottom gradient */
fx_fill_rect_gradient(20, 140, 240, 170, FX_RED_ACCENT, FX_YELLOW, 0);  /* red→yellow left-right gradient */
```

> Note: gradient/anti-aliasing both require being able to **read back the target pixels**, so they take the **offscreen/framebuffer** path (a canvas automatically goes offscreen when AA is enabled by default).

### 8.1a Anti-Aliasing (v2.2, off by default, must be enabled)

`fx_set_aa(1)` enables anti-aliasing (**off by default**; `FX_AA_DEFAULT=1` changes the default to on, `fx_set_aa(0)` turns it off at runtime).
`line/circle/fill_circle/rect/fill_rect_round/arc/ellipse/fill_ellipse` do edge blending based on "distance to the shape / subpixel coverage", giving smooth, jagged-free edges.
**Once enabled, the canvas automatically goes offscreen** to support blending (no manual `fx_canvas_set_buf` needed).
> Note: it is off by default to **avoid turning every canvas into offscreen/CPU rendering** (otherwise pages designed for direct/GPU drawing, such as scrolling pages, would be affected). Call `fx_set_aa(1)` only where smoothing is needed.

```c
fx_set_aa(1);               /* make subsequent canvases auto go offscreen + anti-alias */
fx_canvas_new(pixel("6,32","444,236"), anim(1), call(on_draw));
...
fx_set_aa(0);               /* off (restore hard edges/direct drawing) */
```

### 8.1b Canvas Convenience (v2.2)

```c
int cw, ch; fx_canvas_size(w, &cw, &ch);    /* get canvas local width/height (replaces the repeated fx_widget_rect+cw/ch) */
fx_canvas_clear(w, FX_WINDOW_BG);           /* one-click clear to color */
```

### 8.2 Polygons

```c
int16_t tri[6] = { 0, -20, 17, 10, -17, 10 };
fx_set_color(FX_YELLOW);
fx_fill_polygon(tri, 3);      /* triangle (point array x,y interleaved) */
fx_draw_polygon(tri, 3);      /* outline */
```

### 8.3 Text

```c
/* base font size (default 18) */
fx_draw_text_c(10, 12, "Hello", FX_WHITE, FX_RGB(30,30,30));
/* specified font size + measure width and center */
int tw = fxtk_text_width_size(16, "Centered");
fxtk_draw_text_size(16, (cw - tw) / 2, 10, "Centered", FX_WHITE, FX_BLACK);
```

### 8.4 Clipping

```c
fx_set_clip(x1, y1, x2, y2);      /* subsequent drawing is limited to this region */
fx_reset_clip();                  /* restore full canvas */
```

### 8.5 Typical Canvas Callback Template

```c
static void on_view(fx_widget_t *w, void *ud) {
    int cw, ch; fx_canvas_size(w, &cw, &ch);        /* v2.2: get canvas local width/height */
    fx_canvas_clear(w, FX_WINDOW_BG);               /* v2.2: one-click clear */

    fx_set_color(FX_RGB(33,150,243));
    fx_fill_rect(10, 10, cw - 10, 40);              /* content */
    fx_set_color(FX_RGB(40,40,40));
    fx_draw_text_c(20, 18, "Status: OK", FX_WHITE, FX_RGB(33,150,243));
}
```

> The older style (still usable) is `fx_widget_rect` + manual `fx_set_color`+`fx_fill_rect` to clear; v2.2's
> `fx_canvas_size` / `fx_canvas_clear` specifically replace these two boilerplate snippets, making canvas code shorter.

---

## 9. Input: Touch/Keyboard/Wheel

> This section's APIs (`fx_touch_state`/`fx_last_key`/`fx_pressed`/`fx_wheel_take`/`fx_set_focus`/`fx_get_focus`, etc.) belong to the **desktop extension**; they are all declared in `fxtk_desktop.h`, and you must `#include "fxtk_desktop.h"` before using them (same for desktop widgets such as the text box).

### 9.1 Mouse/Touch State

```c
int mx, my, mp;
fx_touch_state(&mx, &my, &mp);     /* coordinates + whether pressed; hover also updates real-time */
```

- Coordinates are **window logical coordinates** (after scaling, in the same coordinate system as widget x1..y2).
- `fx_pressed()`: returns the currently pressed widget pointer (non-NULL means pressed), usable for custom-drawn pressed state or drag detection.

```c
static void on_canvas(fx_widget_t *w, void *ud) {
    int mx, my, mp; fx_touch_state(&mx, &my, &mp);
    if (mp && fx_pressed() == w) { ... /* pressing on the canvas */ }
}
```

### 9.2 Keyboard

```c
fx_keyev_t k = fx_last_key();          /* most recent key press */
if (k.utf8[0])      ...  /* printable character (UTF-8) */
else if (k.key == FX_KEY_UP)    ...  /* arrow key */
else if (k.key == FX_KEY_RETURN) ...  /* Enter */
```

Function-key enum: `FX_KEY_BACKSPACE/RETURN/ESCAPE/LEFT/RIGHT/HOME/END/UP/DOWN/DELETE`.

### 9.3 Wheel and Core Scrolling

The driver accumulates wheel pixel deltas (`fx_poll` auto-routes them to the widget under the cursor). **For custom-drawn scrolling, a one-line library API is recommended**:

```c
int off = fx_scroll_update(w, total);   /* core scrolling: wheel/interpolation/repaint all in the library, returns current offset */
fx_scrollbar_draw(w, off, total);       /* library scrollbar: track+thumb, supports mouse drag */
```

Key points:
- **Feel**: target pixels accumulate + 25% interpolation per frame (rc-build feel); it settles quickly when the wheel stops, no inertia lag;
- **Zero repaint**: it does not request a repaint when the offset is unchanged;
- **Multiple widgets**: an internal 8-slot state pool lets multiple canvases scroll in parallel;
- **Drag**: the scrollbar thumb supports mouse drag (including on custom-drawn canvases; the state pool syncs while dragging);
- The low-level primitive `int dy = fx_wheel_take(w);` is still available when you need fully manual control.

---

## 10. Runtime Widget Operations

| Function | Description |
|---|---|
| `fx_find("name")` | look up a widget by its unique name |
| `fx_set_title(w, s)` | change the title and repaint |
| `fx_set_value(w, v)` | change the value (slider/progress/checkbox) 0~100 |
| `fx_get_value(w)` | read the value |
| `fx_set_color_w(w, c)` | change the background color and repaint |
| `fx_set_fgcolor(w, c)` | change the foreground color |
| `fx_set_cb(w, fn, ud)` | replace callback + user data |
| `fx_set_visible(w, v)` | show/hide |
| `fx_widget_rect(w, &x1,&y1,&x2,&y2)` | get the current rectangle (screen coords) |
| `fx_widget_set_rect(w, x1,y1,x2,y2)` | move/resize a widget (auto repaints old and new regions) |
| `fx_widget_type(w)` / `fx_widget_title(w)` | query type / title |
| `fx_set_fontsize(w, n)` / `fx_set_align(w, a)` | font size / alignment |

### 10.1 Moving Widgets (animation/load test)

```c
int bx = x1 + 20 + (int)((cw - 170) * (0.5f + 0.5f * sinf(t * 0.023f)));
fx_widget_set_rect(b, bx, by, bx + 120, by + 36);
```

### 10.2 Creating Widgets at Runtime (dynamic add)

```c
static fx_widget_t *s_dyn[4000];      /* pointer cache */
if (fxtk_fps() >= 30 && n < cap) {
    fx_parent(fx_find("move_cv"));
    fx_widget_t *nb = fx_button_new(pixel("0,0","0,0"), title("Dynamic"),
                                    color(FX_RGB(156,39,176)), call(on_click));
    fx_parent(fx_find("tab"));
    fx_widget_fix(nb, x, y);          /* fix coordinates: prevent layout recalc reset */
    fx_widget_set_rect(nb, x, y, x+20, y+16);
    s_dyn[n++] = nb;
}
```

- `fx_widget_fix(w, x, y)`: switch to **fixed-coordinate mode** (`FX_POS_FIXED`); subsequent `fx_layout()` (triggered by resize/new widget) will not reset it with ox/oy, and it records the movement reference point.
- The widget pool is **4096** slots by default (`FX_MAX_WIDGETS`); `fxtk_widget_count()` checks the current live count.
- For dynamic widgets, cache pointers in an array to avoid a linear `fx_find` scan every frame.

### 10.3 Deleting Widgets

```c
fx_widget_t *w = fx_find("tmp");
if (w) fx_delete(fx_wptr(w));         /* locate and delete via fx_wptr(widget pointer) or grid(name) */
```

---

## 11. Images and Effects

### 11.1 Create/Load Images (`fxtk_image.h` + SDL_image)

```c
fx_image_t *img = fx_image_create(w, h);      /* blank image, renderable offscreen */
fx_image_set_px(img, x, y, color);            /* fill pixel by pixel */
fx_image_free(img);                           /* free */

/* generate a procedural texture by drawing */
static fx_image_t *make_pic(int kind) {
    fx_image_t *im = fx_image_create(120, 90);
    for (int y = 0; y < 90; y++)
        for (int x = 0; x < 120; x++)
            fx_image_set_px(im, x, y, FX_RGB(x * 255 / 119, y * 255 / 89, 140));
    return im;
}
```

> On the PC side there is also `fx_image_load(path)` (SDL_image decode; see demo-main/fxtk_image_sdl.c).

### 11.2 Image Widget

```c
fx_image_new(pixel("6,32","280,220"), name("pic"), image(img), call(on_img));
fx_set_image(w, new_img);             /* swap image */
fx_image_set_zoom(w, 150);            /* scale 10~400% */
```

### 11.3 Effects (`fxtk_effects.h`, CPU pixel-level)

| Function | Description |
|---|---|
| `fx_draw_image(img,x,y,dw,dh)` | scale-draw onto the canvas |
| `fx_draw_image_ex(img,x,y,dw,dh,dark)` | same as above, dark=darken by percentage |
| `fx_draw_image_rot(img,cx,cy,deg,pct)` | rotate + scale texture (GPU hardware rotation) |
| `fx_image_flip_x/y(img)` | horizontal mirror / vertical flip |
| `fx_image_grayscale(img)` | grayscale |
| `fx_image_tint(img,c,amount)` | tint 0~255 |
| `fx_image_brightness(img,delta)` | brightness -255~255 |

```c
fx_draw_image_rot(s_pics[0], cw/4, ch/2, ang, 90);   /* rotate texture */
fx_image_grayscale(pic);                             /* grayscale first */
```

---

## 12. Text and Fonts

- Base font size 18 (`fxtk_font_init` sets it).
- `fxtk_draw_text_size(size,...)`: any font size, using an internal **font cache** (8 TTFs; when full, least-recently-used fonts are evicted and closed).
- Text is cached as a **texture** keyed by (font, text, fg, bg) with LRU eviction — repeated text has zero CPU cost.
- Chinese fonts: built-in fallback (wqy-zenhei / Noto Sans CJK / msyh), `FXTK_FONT` overrides.

```c
fxtk_draw_text_size(16, x, y, "Large font", FX_WHITE, FX_BLACK);
int w = fxtk_text_width_size(16, "Large font");   /* measure width */
```

---

## 13. Popups and Context Menus

- **Context menu**: right-click on an input box pops up an on-top menu (copy/paste/select all); `fxtk_right_click` is reported by the driver.
- **Dropdown popup**: the option layer opened by `fx_drop` is a top-level popup, drawn above all widgets and not clipped by pages.
- Near the bottom it automatically pops upward; content overflows with scrolling; clicking outside auto-closes.

---

## 14. Repaint and Performance Model

### 14.1 Repaint Mechanism

| Trigger | Behavior |
|---|---|
| `fx_repaint()` | full repaint |
| `fx_repaint_rect(x1,y1,x2,y2)` | dirty-region repaint (in the current implementation promoted to a full repaint to guarantee no ghosting) |
| widget property change (`fx_set_title` etc.) | automatic local repaint |
| `anim(1)` canvas | callback + repaint **every frame** |

### 14.2 Render Pipeline (GPU)

- Widget draw → SDL vertex batch (rectangles/triangles/polylines submitted at once) → text-texture blit → present.
- Image scaling/rotation goes through GPU texture transforms; the CPU only computes 24bit RGB pixels.
- `FXTK_STAT=1` prints vertex count and frame time for performance tuning.

### 14.3 Performance Tips

1. **Static content**: don't use `anim(1)`; when it changes, call `fx_repaint_rect` manually.
2. **Animated content**: use an `anim(1)` canvas; avoid `fx_find` every frame inside it (cache pointers).
3. **Many particles/primitives**: draw into a small offscreen image then scale it up with `fx_draw_image` (GPU blit), e.g. the particle page's 2x downscaled buffer.
4. **Dynamically adding widgets**: the widget pool caps at 4096; add only when the frame rate is comfortable (≥30fps) to avoid runaway.
5. **Text**: identical text/color auto-runs the texture cache; avoid concatenating volatile strings that become keys.

---

## 15. Complete Example: A Waveform Page from Scratch

Goal: a page with "real-time waveform + speed slider + switch", fully demonstrating label/button/slider/checkbox/canvas collaboration.

```c
#include "fxtk.h"
#include <math.h>

static int s_phase = 0, s_speed = 30, s_on = 1;

/* canvas callback: draw waveform every frame */
static void on_wave(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;

    fx_set_color(FX_RGB(30, 30, 30));
    fx_fill_rect(0, 0, cw - 1, ch - 1);          /* clear background */

    if (!s_on) return;                            /* switch off, don't draw */

    int amp = ch / 2 - 8, prev_y = ch / 2;
    fx_set_color(FX_YELLOW);
    for (int x = 0; x < cw; x++) {                /* dual-frequency superimposed waveform */
        float t = (float)(x + s_phase) * 0.1f;
        int y = ch / 2 + (int)(amp * (0.7f * sinf(t) + 0.3f * sinf(t / 3.0f)));
        fx_draw_line(x - 1, prev_y, x, y);
        prev_y = y;
    }
    s_phase += s_speed;                           /* phase advance */
    if (s_phase > 4096) s_phase -= 4096;
}

/* slider callback */
static void on_speed(fx_widget_t *w, void *ud) {
    (void)ud;
    s_speed = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), s_speed);  /* link the progress bar */
}

/* switch callback */
static void on_wave_sw(fx_widget_t *w, void *ud) {
    (void)ud;
    s_on = fx_get_value(w) != 0;
}

void build_wave_page(void) {
    fx_tab_new(pixel("10,26", "470,266"), title("Waveform, Graphics, Controls"),
               name("tab"), color(FX_RGB(224,224,224)));
    fx_parent(fx_find("tab"));

    fx_canvas_new(pixel("6,32", "444,188"), name("wave_cv"), page(0),
                  anim(1), color(FX_RGB(30,30,30)), call(on_wave));
    fx_label_new(pixel("6,194", "56,212"), page(0), title("Speed"));
    fx_slider_new(pixel("60,194", "292,212"), name("speed"),
                  page(0), value(30), color(FX_RGB(76,175,80)), call(on_speed));
    fx_progress_new(pixel("300,196", "444,210"), name("speed_bar"),
                    page(0), value(30));
    fx_checkbox_new(pixel("6,218", "150,240"), name("wave"),
                    page(0), title("Waveform switch"), value(1), call(on_wave_sw));

    fx_parent(NULL);
}

/* entry only needs app_init() (main_linux.c provides the main loop) */
void app_init(void) { build_wave_page(); }
```

---

## 16. Common Pitfalls and Best Practices

### Common Pitfalls

1. **Coordinates in a callback are local**: they start at 0; don't add the widget's screen position. Get the widget position with `fx_widget_rect`.
2. **Only `anim(1)` draws every frame**: after changing a static canvas's content, call `fx_repaint_rect` manually.
3. **`page(n)` must be used with a tab**: otherwise the widget does not show/hide with page switching (it's displayed directly on the root).
4. **Change the rectangle with `fx_widget_set_rect`**: directly modifying `w->x1` (the struct is opaque) will not trigger a repaint.
5. **Widget names are globally unique**: `fx_find` linearly scans the whole pool; a duplicate name returns the first one.
6. **Dynamically created widgets get reset by layout**: after creating, you must `fx_widget_fix(w,x,y)` to fix coordinates, otherwise the next `fx_layout()` pulls it back to the design coordinate origin.
7. **`fx_find` every frame is O(4096)**: in hot loops use pointer caches (e.g. the `s_dyn[]` in the load-test page).
8. **Use `line()`/`fx_set_fontsize` for font size**: for custom-drawn text use `fxtk_draw_text_size`, don't hand-roll scaled font sizes.
9. **Colors are 24bit RGB**: `0xRRGGBB`; use `FX_RGB(r,g,b)` or hex literals directly (e.g. `0xFF0000` red).
10. **Window close/exit**: SDL auto-calls `exit(0)` on receiving a QUIT event; no handling needed.

### Best Practices Checklist

- Use the default palette for all static UI; only highlights get an explicit `color()`.
- Prefer `pixel()` design coordinates for layout; use `percent()` to fill; use `grid()` for tables.
- Give a widget a `name()` if it's interactive; purely display widgets can be anonymous.
- Don't make animation frequencies exceed 60fps; inside an `anim(1)` canvas, `fill_rect` clear the background before drawing.
- Free offscreen images (`fx_image_create`) with `fx_image_free` when done.
- For large lists/scrolling, prefer "canvas custom-drawn + visible-row clipping" (see `on_scroll_view`), don't pile up thousands of child widgets.

---

## Appendix: Drivers and Porting

The fxtk core abstracts the platform through `fx_driver_t`:

```c
typedef struct {
    uint16_t width, height;
    int  (*init)(void);
    void (*set_window)(x0,y0,x1,y1);      /* pixel stream window */
    void (*push_pixels)(px, n);           /* push pixels */
    void (*fill_rect)(x0,y0,x1,y1,color); /* rectangle (driver-accelerated) */
    int  (*touch_read)(x,y,pressed);
    int  (*key_read)(fx_keyev_t*);
    void (*blit_img)(...);                /* image scale blit */
    void (*fill_tri)(...);                /* GPU triangle */
    void (*draw_line)(...);               /* GPU polyline */
    void (*blit_tex)(...);                /* text texture blit */
    void (*blit_img_rot)(...);            /* GPU rotation blit */
    ...
} fx_driver_t;
```

The PC implementation is in `demo-main/fxtk_sdl_driver.c`; porting to another platform only requires implementing this struct and calling `fx_init(&drv)`.

## Appendix: Themes, Scroll Containers and GPU Ray Tracing

### Themes (light/dark two palettes)

Colors provide two sets of values with `fx_colorx_t { light, dark }`; the current theme auto-selects one:

```c
fx_set_global_background((fx_colorx_t){ FX_RGB(245,245,245), FX_RGB(30,30,34) });
fx_set_dark_theme(1);                /* 1=dark, 0=light */
int dark = fx_is_dark_theme();
fx_color_t c = fx_colorx_current(some_colorx);   /* take the value by the current theme */
```

(`fx_set_bg` only changes the light background; when you call `fx_set_dark_theme`, widgets are uniformly refreshed.)

### Scroll Containers

`fx_scroll_new(...)` creates a scrollable container; when the content height exceeds it, a scrollbar appears:

```c
fx_widget_t *sc = fx_scroll_new(pixel("0,0","480,200"));
fx_parent(sc);                       /* subsequent widgets hang into the scroll container */
fx_button_new(pixel("10,10","200,50"), title("Item 1"), call(...));
...
fx_scroll_content(sc, 1200);         /* total content height (pixels), scrollable only when exceeded */
```

When you need finer control, use smooth scrolling: `int off = fx_scroll_update(w, content_h)` (target pixels + 25% per-frame interpolation), combined with `fx_scrollbar_draw(w, off, content_h)` to draw the scrollbar.

### GPU Ray Tracing (3D page)

The demo's 3D page uses Shadertoy-style software ray marching (`raymarch_render`) rendered into an offscreen buffer; an optional GPU (GLSL) path `gpu_raymarch_render` is available:

```c
raymarch_render(px, w, h, time, max_steps);   /* CPU software ray tracing, 24bit RGB */
gpu_raymarch_start();                          /* start GPU thread */
if (gpu_raymarch_ok()) gpu_raymarch_render(px, w, h, time);   /* GPU first */
const char *r = gpu_raymarch_renderer();       /* return the renderer name */
```

`gpu_raymarch_ok()` returns 0 and it auto-falls back to CPU (on VM/CI environments without hardware GL); on Windows, `gpu_stub_win.c` provides a no-op implementation. The scene includes a reflective floor/spheres + stepping rings + soft shadows + fog.

> If you want to understand the source details of widget drawing, layout, and repaint, see `docs/internals.md`.
