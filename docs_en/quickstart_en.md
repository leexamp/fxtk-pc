# fxtk Quickstart (PC / Linux Simulator)

For the full tutorial see `docs/guide.md`; this page gets you running in 5 minutes.

## Building and Running

Run `./build.sh` in the `demo-main` directory; it automatically compiles and runs `fxtk_sim` (the complete 12-page demo, including 3D ray tracing / GPU particles). The window can be freely resized or even made fullscreen, and the UI scales proportionally to fill it.

```bash
cd demo-main
./build.sh                # the full demo
./build_ex.sh ex03_anim   # any standalone example (ex01~ex18)
```

Dependencies: `libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev` (`sudo apt install`).

## Build a UI in Three Steps

**1. Create widgets** (they look good even without specifying a color — a default of light background with dark text):

```c
fx_label_new(percent("0.02,0.01","0.98,0.09"),
             title("My App"));                    /* default dark text */
fx_button_new(pixel("100,100","220,136"), name("btn"),
              title("Click Me"), call(on_click)); /* default blue bg white text */
```

**2. Write the callback**:

```c
static void on_click(fx_widget_t *w, void *ud) {
    static int n = 0;
    char b[32]; snprintf(b, sizeof b, "clicked %d times", ++n);
    fx_set_title(w, b);          /* auto repaint */
}
```

**3. Self-draw a canvas** (immediate mode):

```c
static void on_draw(fx_widget_t *w, void *ud) {
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    fx_set_color(FX_RGB(30,30,30)); fx_fill_rect(0,0,cw-1,ch-1);
    fx_set_color(FX_YELLOW); fx_draw_circle(cw/2, ch/2, ch/3);
}
fx_canvas_new(pixel("6,32","444,236"), anim(1), call(on_draw));
```

## Choosing a Layout

- A small fixed widget → `pixel()` (480×272 design coordinates, scaled proportionally).
- Fill/align the parent container → `percent()` (0.0~1.0).
- Tables → `grid()`.

## Common Operations

- Change text `fx_set_title`; change rect `fx_widget_set_rect`;
- Find a widget `fx_find("name")`; switch parent `fx_parent(...)`;
- Read touch `fx_touch_state`; read key `fx_last_key`;
- For dynamic widgets, fix coordinates with `fx_widget_fix`; the count cap is set by the widget pool (4096).

## Fullscreen Stress Test

The demo's "stress test" page automatically adds widgets every frame and lets them drift while frame rate is to spare (≥30fps), until it fills the canvas — at fullscreen 1080P it can hold several thousand. The title shows the total widget count in real time (`fxtk_widget_count()`).

## Environment Variables

| Variable | Effect |
|---|---|
| `FXTK_FONT=...` | Specify a font |
| `FXTK_STAT=1` | Print rendering stats every second |
| `FXTK_BENCH=1` | Disable vsync to measure the frame rate |
| `FXTK_MAXW=1920` | Scaling cap |
