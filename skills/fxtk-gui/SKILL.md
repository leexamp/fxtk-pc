---
name: fxtk-gui
description: Write a small GUI program in C with the fxtk framework — a single-file binary, no third-party DLLs. Use when the user asks for a window, panel, buttons, labels, sliders, text box, canvas drawing, animation, or a small desktop tool in C. Includes the exact API, a verified template, the compile command, and the common traps that break the build.
---

# Writing fxtk GUI programs

fxtk is a C99 GUI framework: widgets are created with attribute macros, the UI is laid out on a
**480×272 design canvas** and scaled automatically to the window. You link it and get one exe with
no third-party DLLs.

**Golden rule for small models: copy the template below verbatim, then change only widget lines.
Do not invent API names.** Every function in this file exists and the template compiles with zero
warnings. Names that look plausible but do NOT exist are listed in "Never use these".

---

## 1. The template (this compiles — verified)

Write this exact shape. `main()` is provided by the shell: **never write your own `main`.**

```c
#include "fxtk.h"          /* core widgets + drawing */
#include "fxtk_desktop.h"  /* fx_textedit_new, fx_scroll_new, maxlen, fx_set_fgcolor */
#include "fxtk_app.h"      /* the shell that provides main() */
#include <stdio.h>

static fx_widget_t *s_label = 0;   /* keep pointers you need to update later */

static void on_click(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    static int n = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "clicked %d", ++n);
    if (s_label) fx_set_title(s_label, buf);   /* setting text auto-repaints */
}

void fxtk_app_init(void)           /* REQUIRED entry point — the shell calls this */
{
    fx_set_bg(FX_RGB(245, 245, 247));

    fx_label_new(pixel("12,10", "300,34"),  title("My tool"));
    s_label = fx_label_new(pixel("12,38", "300,60"), title("clicked 0"));

    fx_button_new(pixel("12,70", "150,104"), title("Go"), call(on_click));
    fx_checkbox_new(pixel("160,70", "300,104"), title("Option"));
    fx_slider_new(pixel("12,112", "300,128"), value(60));
    fx_progress_new(pixel("12,134", "300,148"), value(40));
}

/* Optional frame callback (animation / timers). Comment it out if not needed. */
/* void fxtk_app_frame(void) { } */

/* Optional window title / size. */
/* const char *fxtk_app_title(void) { return "My tool"; } */
/* void fxtk_app_size(int *w, int *h) { *w = 960; *h = 540; } */
```

### Build it

Put the file in `your_app/main.c` (that directory already has a `Makefile`), then:

```bash
cd your_app && make && ./your_app
```

To compile standalone instead (paths relative to the repo root):

```bash
gcc -Os -Icomponents/fxtk -Idrivers -Ithird_party/sokol \
    app.c \
    drivers/fxtk_app_sokol.c drivers/fxtk_sokol_driver.c drivers/fxtk_font_stb.c \
    components/fxtk/fxtk.c components/fxtk/fxtk_draw.c components/fxtk/fxtk_widgets.c \
    components/fxtk/fxtk_effects.c components/fxtk/fxtk_extra.c components/fxtk/fxtk_backends.c \
    -o app -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
```

Linux deps (only if missing): `sudo apt install libx11-dev libxcursor-dev libxi-dev libgl1-mesa-dev`

---

## 2. Widget creation — exact forms

Every widget is `fx_<kind>_new(attrs...)`, where attrs are macros separated by commas.
**Position: always `pixel("x1,y1", "x2,y2")` or `percent("x1,y1", "x2,y2")` — both arguments are
comma-separated STRING pairs, not four numbers.**

| Widget | Exact call |
|---|---|
| Label | `fx_label_new(pixel("12,10","300,34"), title("Hi"))` |
| Button | `fx_button_new(pixel("12,70","150,104"), title("Go"), call(on_click))` |
| Checkbox | `fx_checkbox_new(..., title("On"), call(on_check))` → read with `fx_get_value(w)` |
| Slider | `fx_slider_new(..., value(60), call(on_slide))` |
| Progress | `fx_progress_new(..., value(40))` |
| Text box | `fx_textedit_new(..., title("text"), maxlen(20))` → read with `fx_textedit_text(w)` |
| Canvas | `fx_canvas_new(..., anim(1), call(on_draw))` |
| Panel / container | `fx_panel_new(..., grid("name"))` then set `fx_parent(panel)` before children |
| Tab page | `fx_tab_new(..., title("A,B,C"), sidebar(FX_TAB_LEFT))` + `page(n)` on children |
| List | `fx_list_new("12,40","300,200")` then `fx_list_add(w,"row")` |
| Dropdown | `fx_drop_new("12,40","200,60")` then `fx_drop_add(w,"item")` |

Layout: `pixel()` = fixed coords on the 480×272 design canvas (scales with the window).
`percent("0.05,0.06","0.95,0.18")` = relative to the parent, each value 0.0–1.0.

### Attribute macros (only these exist)

`pixel` `percent` `grid("name")` `title("s")` `text("s")` `name("s")` `call(fn)` `color(c)`
`fgcolor(c)` `border(n)` `radius(n)` `value(n)` `page(n)` `anim(0|1)` `dense()` `line(n)` `row(n)`
`sidebar(FX_TAB_LEFT|RIGHT|TOP|BOTTOM)` `image(img)` `maxlen(n)` (desktop header)

`name("s")` matters: it is how you look the widget up later with `fx_find("s")`.
Colors: `FX_RGB(r,g,b)`, or constants `FX_WHITE FX_BLACK FX_RED FX_GREEN FX_BLUE FX_YELLOW FX_GRAY FX_LGRAY`.

### Callbacks

Signature is always `void fn(fx_widget_t *w, void *ud)`. `w` is the widget that fired.
Get a checkbox/slider value with `fx_get_value(w)`.

---

## 3. Canvas drawing (immediate mode)

Inside a canvas callback you draw in **canvas-local coordinates** (0,0 = top-left of the canvas).

```c
static void on_draw(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw = 0, ch = 0;
    fx_canvas_size(w, &cw, &ch);          /* get local width/height */
    fx_canvas_clear(w, FX_RGB(250,250,252));   /* fill background */

    fx_set_color(FX_RGB(33,150,243));     /* set colour for subsequent calls */
    fx_fill_circle(cw/2, ch/2, ch/4);
    fx_fill_rect(10, 10, 100, 60);
    fx_draw_rect(2, 2, cw-3, ch-3);       /* outline */
    fx_draw_line(0, 0, cw-1, ch-1);
    fx_draw_text(8, 8, "label");

    fx_set_color(FX_RGB(244,67,54));
    fx_fill_rect_gradient(10, 80, 100, 140, FX_RED, FX_BLUE, 1);  /* vertical=1 */
}
```

Drawing primitives: `fx_set_color(c)` `fx_draw_pixel` `fx_draw_line` `fx_draw_hline` `fx_draw_vline`
`fx_draw_rect` `fx_fill_rect` `fx_draw_rect_round` `fx_fill_rect_round` `fx_draw_circle` `fx_fill_circle`
`fx_draw_text` `fx_draw_text_c` `fx_fill_rect_gradient`.

Use `anim(1)` on the canvas when you want the callback to run every frame (animation).
Without it the canvas is drawn once / on change.

---

## 4. Updating widgets at runtime

```c
fx_widget_t *w = fx_find("btn");      /* look up by name() */
fx_set_title(w, "new text");          /* change text  — auto repaints */
fx_set_value(w, 80);                  /* slider/progress 0..100 */
fx_set_color_w(w, FX_RGB(255,90,0));  /* background */
fx_set_fgcolor(w, FX_RGB(40,40,40));  /* text / foreground */
fx_set_visible(w, 0);                 /* hide */
fx_widget_set_rect(w, 10, 10, 200, 60);
int v = fx_get_value(w);
```

**You never call `fx_repaint()`** — the setters repaint for you.

---

## 5. Never use these (they do NOT exist — this is where models hallucinate)

| Wrong | Right |
|---|---|
| `main()` in your file | provided by the shell; write `fxtk_app_init()` |
| `app_init()` (in app-shell mode) | `fxtk_app_init()` — `app_init` is only for the demo's `main_linux.c` |
| `fx_rect(...)` / `fx_new_button(...)` | `pixel()/percent()` + `fx_button_new()` |
| `pixel(10,10,150,40)` | `pixel("10,10","150,40")` — two strings |
| `fx_textedit_set_readonly` | **only exists in v2.4.5+**; on older checkouts it fails to link. Guard with `#ifdef FXTK_HAVE_READONLY` (see `ref_app.c`) if you must support older trees |
| `fxtk_font_height(n)` | declared in `fxtk.h` but **has no implementation** — linking it fails |
| `fx_create_*`, `fx_add_widget`, `fx_widget_create` | not in this API |
| `fx_set_text` | `fx_set_title` |
| `fx_draw_string` | `fx_draw_text` / `fx_draw_text_c` |
| `SDL_*` / `sokol_*` in your app | the shell hides the backend; never include them |
| calling `fx_repaint()` after every change | unnecessary; setters repaint |
| forgetting `#include "fxtk_desktop.h"` | `fx_textedit_new`/`fx_scroll_new`/`maxlen` are declared there, not in `fxtk.h` |

Other rules:
- One translation unit is fine — put everything in one `.c` file.
- A missing `#include "fxtk_desktop.h"` gives `implicit declaration of function 'fx_textedit_new'`.
- Widget pool is capped (8192 on PC): if you create widgets in a loop forever, creation eventually
  returns `NULL`. Guard with `if (w)`.
- Design coordinates are 480×272; anything outside that box may be clipped or scaled oddly.

---

## 6. Reference implementation

`ref_app.c` in this directory is a verified program exercising label, button, checkbox, slider,
progress, text box, read-only toggling and an animated canvas. It compiles with **zero warnings**.

```bash
cd skills/fxtk-gui && ./verify.sh      # compiles ref_app.c and reports pass/fail
```

Use it as the ground truth when unsure: copy the nearest block from it rather than inventing.
