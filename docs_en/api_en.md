# fxtk API Reference

> `components/fxtk/fxtk.h` is the authoritative list. Colors are 24-bit RGB (`0xRRGGBB`, `uint32_t`).
> See `guide.md` for the full tutorial.

## Attribute Macros

| Macro | Description |
|---|---|
| `pixel("x1,y1","x2,y2")` | 480×272 design coordinates, scaled proportionally |
| `percent("a,b","c,d")` | **0.0~1.0 float**, relative to the parent widget |
| `grid("name",r1,c1,r2,c2)` | Named-grid cell region; `grid("name")` fills the whole grid |
| `title(s)` / `name(s)` | Text / widget name (globally unique) |
| `call(fn)` | Callback `void fn(fx_widget_t*,void*)` |
| `color(c)` / `fgcolor(c)` | Background / foreground color |
| `line(n)` | Font size |
| `row(n)` | Alignment 0=left 1=center 2=right |
| `value(n)` | Initial value 0~100 |
| `maxlen(n)` | Input box max length (`fxtk_desktop.h`) |
| `anim(n)` | 1=redraw every frame |
| `page(n)` | Attach to tab page n |
| `border(n)` / `radius(n)` | Border / corner radius |
| `dense()` | grid dense mode |
| `image(img)` | image widget initial image |

## Widget Creation

```c
fx_widget_t *fx_label_new(...);     /* text */
fx_widget_t *fx_button_new(...);    /* button */
fx_widget_t *fx_canvas_new(...);    /* canvas (immediate-mode callback) */
fx_widget_t *fx_slider_new(...);    /* slider */
fx_widget_t *fx_progress_new(...);  /* progress */
fx_widget_t *fx_checkbox_new(...);  /* checkbox */
fx_widget_t *fx_textedit_new(...);  /* input box (fxtk_desktop.h) */
fx_widget_t *fx_list_new_p("r1","r2",pg);    /* list (pg=tab page index, -1=none) */
fx_widget_t *fx_drop_new_p("r1","r2",pg);    /* dropdown (pg=tab page index, -1=none) */
fx_widget_t *fx_tab_new(...);  fx_widget_t *fx_panel_new(...);
fx_widget_t *fx_grid_map(...); fx_widget_t *fx_image_new(...);
```

Example:

```c
fx_button_new(pixel("340,40","400,70"), name("br_r"), page(6),
              title("red"), color(FX_RGB(244,67,54)), call(on_brush));
fx_widget_t *d = fx_drop_new_p("220,106","360,132", 10);
fx_drop_add(d,"Font: Small"); fx_list_set_cb(d, on_drop);
```

## Common Operations

```c
fx_widget_t *fx_find(const char *name);
void fx_parent(fx_widget_t *p);                 /* set default parent, NULL=root */
void fx_set_title(fx_widget_t*, const char*);
void fx_set_color_w(fx_widget_t*, fx_color_t);
void fx_set_fgcolor(fx_widget_t*, fx_color_t);
void fx_set_value(fx_widget_t*, int v);         /* 0~100 */
int  fx_get_value(const fx_widget_t*);
void fx_set_cb(fx_widget_t*, void(*)(fx_widget_t*,void*), void*);
void fx_set_visible(fx_widget_t*, int);
void fx_widget_rect(fx_widget_t*, int*,int*,int*,int*);
void fx_widget_set_rect(fx_widget_t*, int x1,int y1,int x2,int y2);
void fx_widget_fix(fx_widget_t*, int x,int y);  /* fix coordinates, prevent layout reset */
void fx_repaint(void);
void fx_repaint_rect(int x1,int y1,int x2,int y2);
void fx_delete(fx_wptr(w));                     /* delete widget (locate via fx_wptr or grid(name)) */
int  fxtk_widget_count(void);                   /* total live widgets */
int  fxtk_fps(void);                            /* frame rate */
int  fxtk_ui_scale(void);                       /* 480 wide=100 */
void fx_set_max_scale(float f);                 /* widget scale cap (times) */
```

## Drawing Primitives (inside a canvas callback, local coordinates)

```c
void fx_set_color(fx_color_t);
void fx_fill_rect(int x1,int y1,int x2,int y2);
void fx_fill_rect_gradient(int x1,int y1,int x2,int y2,fx_color_t c1,fx_color_t c2,int vertical); /* v2.2 gradient, vertical=1 top-bottom */
void fx_draw_rect(int x1,int y1,int x2,int y2);
void fx_draw_hline(int x1,int x2,int y);
void fx_draw_vline(int x,int y1,int y2);
void fx_draw_line(int x1,int y1,int x2,int y2);
void fx_draw_circle(int cx,int cy,int r);       void fx_fill_circle(...);
void fx_draw_ellipse(int cx,int cy,int rx,int ry);  void fx_fill_ellipse(...);
void fx_draw_arc(int cx,int cy,int r,int a1,int a2); void fx_fill_arc(...);
void fx_draw_rect_round(int x1,int y1,int x2,int y2,int r);
void fx_fill_rect_round(int x1,int y1,int x2,int y2,int r);
void fx_draw_triangle(int x1,int y1,int x2,int y2,int x3,int y3);
void fx_fill_triangle(int x1,int y1,int x2,int y2,int x3,int y3);
void fx_draw_polygon(const int16_t *pts, int n);
void fx_fill_polygon(const int16_t *pts, int n);
void fx_draw_text_c(int x,int y,const char*,fx_color_t fg,fx_color_t bg);
void fxtk_draw_text_size(int size,int x,int y,const char*,fx_color_t fg,fx_color_t bg);
int  fx_text_width(const char*);
int  fxtk_text_width_size(int size,const char*);
void fx_set_clip(int x1,int y1,int x2,int y2);
void fx_reset_clip(void);
```

## Canvas (v2.2 convenience API)

Inside a canvas callback you draw using local coordinates (0,0)~(cw-1,ch-1); see the primitives in the previous section.

```c
void fx_canvas_begin(fx_widget_t *cv);   /* enter canvas drawing (framework auto-calls, usually no need to do manually) */
void fx_canvas_end(void);
int  fx_canvas_enable_buf(fx_widget_t *cv);          /* enable offscreen buffer, 0=success */
void fx_canvas_set_buf(fx_widget_t *cv, int on);     /* enable/disable offscreen buffer */
void fx_canvas_size(fx_widget_t *cv, int *w, int *h);/* get canvas local width/height (in cb, replaces the fx_widget_rect boilerplate) */
void fx_canvas_clear(fx_widget_t *cv, fx_color_t c); /* one-click clear to color c */
```

## Text

```c
void fx_set_fontsize(fx_widget_t*, int size);
void fx_set_align(fx_widget_t*, int a);   /* 0/1/2 */
void *fxtk_font_size(int size);           /* get the font handle for the size */
int  fxtk_font_height(int size);
```

## Input

```c
void fx_touch_state(int *x,int *y,int *pressed);  /* hover is also real-time */
fx_widget_t *fx_pressed(void);
fx_keyev_t fx_last_key(void);   /* .utf8 / .key(FX_KEY_*) */
int  fx_wheel_take(fx_widget_t*);         /* canvas wheel delta */
void fx_textedit_set_readonly(fx_widget_t*, int);
void fx_set_focus(fx_widget_t*);  fx_widget_t *fx_get_focus(void);
```

## Core Scrolling (drop-in for a self-drawn canvas / list rows)

```c
int  fx_scroll_update(fx_widget_t *w, int content_h);
/* Updates the scroll state and returns the current offset (integer):
 *  - Wheel delta accumulates as target pixels, 25% interpolation per frame (rc feel, no inertia lag)
 *  - Internal state pool lets multiple widgets scroll in parallel without interfering
 *  - Automatically requests a repaint when the offset changes (zero repaint when idle)
 *  - Also records w->content_h, supporting scrollbar thumb dragging
 *  In a self-drawn canvas callback: int off = fx_scroll_update(w, total); then draw visible rows by off */
void fx_scrollbar_draw(fx_widget_t *w, int off, int content_h);
/* library scrollbar: track + thumb, follows the off offset */
```

Canvas example (`examples/ex05_scroll.c`, fully runnable):

```c
static void on_view(fx_widget_t *w, void *ud) {
    int off = fx_scroll_update(w, 100 * 36);   /* 100 rows x 36px */
    /* draw visible rows by off ... */
    fx_scrollbar_draw(w, off, 100 * 36);
}
```

## Fonts

```c
void fxtk_font_set_size(int size);   /* fix the current font size (text/width measurement use the same font) */
```


## List / Dropdown Data

```c
void fx_list_add(fx_widget_t*, const char*);
void fx_drop_add(fx_widget_t*, const char*);
void fx_list_set_cb(fx_widget_t*, void(*)(fx_widget_t*,void*));
int  fx_list_sel(fx_widget_t*);           /* selected row index */
```

## Images / Effects

```c
fx_image_t *fx_image_create(int w,int h);
void fx_image_free(fx_image_t*);
void fx_image_set_px(fx_image_t*, int x,int y, fx_color_t);
void fx_draw_image(fx_image_t*, int x,int y,int dw,int dh);
void fx_draw_image_ex(fx_image_t*, int x,int y,int dw,int dh,int dark);
void fx_draw_image_rot(fx_image_t*, int cx,int cy,int deg,int pct);
void fx_image_flip_x(fx_image_t*);  void fx_image_flip_y(fx_image_t*);
void fx_image_grayscale(fx_image_t*);
void fx_image_tint(fx_image_t*, fx_color_t c, int amount);   /* 0~255 */
void fx_image_brightness(fx_image_t*, int delta);             /* -255~255 */
void fx_set_image(fx_widget_t*, fx_image_t*);                 /* image widget */
void fx_image_set_zoom(fx_widget_t*, int pct);                /* 10~400 */
```

## System

```c
void fx_init(const fx_driver_t *drv);
void fx_poll(void);
uint16_t fx_width(void);  uint16_t fx_height(void);
void fx_set_bg(fx_color_t c);   fx_color_t fx_get_bg(void);
void fx_set_window_title(const char*);
void fx_set_touch_debug(int on);
void fx_set_grid_lines(int on);
void fxtk_set_fps_debug(int on);   /* bottom-left FPS badge (default off, only on in demo) */
int  fx_widget_type(const fx_widget_t*);
const char *fx_widget_title(const fx_widget_t*);
/* theme: two palettes light/dark */
void fx_set_dark_theme(int dark);          /* 1=dark */
int  fx_is_dark_theme(void);
void fx_set_global_background(fx_colorx_t c);
fx_color_t fx_colorx_current(fx_colorx_t c);
```

## Default Color Quick Reference

Defaults that apply when you don't specify a color:

| Widget | Background | Foreground |
|---|---|---|
| Window | 245,245,245 | — |
| label / checkbox | Transparent | 40,40,40 dark text |
| Button | 33,150,243 blue | White |
| grid / panel / tab | 245,245,245 | Light-gray grid lines |
| slider / progress | 76,175,80 green | Light-gray track |
| canvas | 245,245,245 | — |
| textedit | White | Black |

## File API (v2.3, `fxtk_fs.h`)

- `int fx_fs_pick_dir(char *out, int cap);` — system folder dialog (Win32 `SHBrowseForFolderW` / Linux `zenity`); returns 1 on success, writes the path (UTF-8).
- `int fx_fs_list(const char *dir, fx_fs_entry_t *out, int max);` — list a folder's contents (Win32 `FindFirstFileW` / POSIX `opendir+stat`); returns count (or -1). `fx_fs_entry_t` = `{char name[256]; int is_dir; long size; char date[32];}`.

The demo's **Components** page uses it for a file browser: folder contents as a table (name/size/date) with a smooth, draggable scrollbar (hover-grows), hover tooltip, row select; a `fx_grid_map(dense())` name editor; and a two-group RGB color picker (button bg / text color).

---

# v2.4 API additions (quick reference)

> New surface added in v2.4. Existing APIs are unchanged (backward compatible).

## Anti-aliasing (two tiers, on by default)
```c
void fx_set_widget_aa(int level);   /* 0=off, 1=widget SDF (default), 2=+ feathered primitives */
int  fx_widget_aa_level(void);
int  fx_aa_autodegrade(void);       /* driver calls this on frame drops: steps one tier down */
```
Runtime override without recompiling: `FXTK_AA=0|1|2`. Backends without the hooks fall back automatically.

## Canvas transform stack
```c
void fx_canvas_push_affine(float a,float b,float c,float d,float e,float f);
void fx_canvas_pop_affine(void);
void fx_transform_reset(void);        /* auto-reset every frame */
int  fx_transform_depth(void);        /* max depth 8 */
void fx_canvas_transform_point(float x,float y,float *ox,float *oy);
void fx_fill_quad(const float *xy8);
```

## Quad warp (true perspective)
```c
void fx_draw_image_quad(const fx_image_t *img, const float *xy8);
int  fx_quad_warp_gpu(void);
int  fx_quad_homography(const float *src8,const float *dst8,float m[9]);
int  fx_mat3_invert(const float *m, float out[9]);
int  fx_quad_corner_weights(const float *xy8, float d4[4]);   /* write into gl_Position.w */
```
Single draw, hardware perspective-correct interpolation (no diagonal seam). Degenerate/self-intersecting
quads fall back to a bounding-box mapping on **both** the GPU and CPU paths.

## GPU raymarch / screenshot / images / IME / logging
```c
int  fx_raymarch_available(void);
void fx_draw_raymarch(float time,int x,int y,int w,int h);
int  fx_screenshot(const char *path);
int  fx_img_load_file(const char *path, fx_img_t *out);   /* PNG/JPEG/BMP/GIF/TGA/PNM */
void fx_set_ime_pos(int x, int y);                        /* X11 IME candidate window placement */
void fx_log(fx_log_level_t lv, const char *fmt, ...);     /* single logging entry (forwards to esp_log_write on ESP32) */
```

## Config macros
| Macro | Meaning | PC default | ESP32 default |
|---|---|---|---|
| `FX_MAX_WIDGETS` | widget pool | **16384** | 4096 |
| `FX_MAX_SCROLL_STATES` | concurrent scroll states | **64** | 8 |
| `FX_MAX_EXTRA_WIDGETS` | list/dropdown slots | **64** | 8 |
| `FXTK_WIDGET_*` | compile-time widget trimming | all on | trim as needed |
| `FXTK_BACKEND_STUB` | deterministic backend | off | — |

The two targets have diverged: PC is not constrained by ESP32 limits; both keep the same API/syntax and
the same rendered result (see the README).

## Debug switches
`FXTK_AA`, `FXTK_RECTDBG`, `FXTK_SDFNOMERGE`, `FXTK_IMEDBG`, `FXTK_IMPORT`, `FXTK_NOVSYNC`,
plus `./build_dbg.sh` / `make fxtk_sim_dbg` for a build with the probes compiled in.
