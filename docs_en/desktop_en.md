# fxtk Desktop Demo (desktop)

`demo-main/app_desktop.c` implements a multi-tab "desktop" demo used to validate all widgets and interactions. The design baseline is 480×272, scaled proportionally to fill any resolution (can go full-screen 1080P).

## Page layout

The top of the interface is a `tab` tab bar (the selected tab is **raised in 3D**: bright background + highlight + dark text; unselected ones are recessed). Child pages are mounted with `page(n)`:

| Page | Content | Validation points |
|---|---|---|
| Waveform | Animated curve + speed slider + switch | `anim(1)` redraws every frame, slider adjusts parameters in real time |
| Graphics | Rotated surface + vector effects + motion trail | `fx_draw_image_rot`, polygons, canvas used as a container |
| Controls | 3×3 grid keyboard + progress linkage | `grid()` layout, value-change callback |
| Image | Surface + zoom/grayscale/tint | `fx_image_set_zoom`, `fx_image_grayscale` |
| 3D | CPU/GPU ray marching | multi-threaded ray tracing, GPU channel switching, FPS label |
| Input | Two text boxes + description | keyboard, Ctrl+A/C/V/X, `maxlen` counting, pixel-level cursor alignment, fixed font size |
| Paint | Drawable canvas + pen-color buttons | touch/mouse drag drawing, offscreen buffer |
| Key-mouse | Real-time keyboard/mouse status | **hover coordinates updated in real time**, key echo |
| Stress test | Moving widgets + **dynamic growth** | auto-adds one widget when frame rate has headroom; can hold thousands at full screen |
| Scroll | 60-row virtual scrolling | `fx_scroll_update` core scrolling (target + 25% interpolation), scroll bar thumb can be dragged |
| Components | List/dropdown/multi-font-size | dropdown pops up upward, popup scrolling stays within bounds |
| Particles | Tens of thousands of particles | offscreen half-size + GPU blit upscale |

## Stress-test page (dynamic widget growth)

- The title shows the **total widget count** in real time (`fxtk_widget_count()`).
- When frame rate ≥30fps, one purple small button (9px font) is created per frame, positioned automatically in a 24×20 grid based on the actual canvas capacity.
- New buttons **sinusoidally drift** like other widgets; the number of layout columns changes with window size.
- At full-screen 1080P the canvas is roughly 1753×810 → can hold **2700+** dynamic buttons (widget pool 4096).
- Dynamic widgets use `fx_widget_fix()` to fix their coordinates, preventing `fx_layout()` from resetting them; pointers are cached in `s_dyn[]` to avoid `fx_find` each frame.

## Key interactions

- **3D tabs**: selected tab has dark text on a bright background + top highlight + bottom bright line (raised), unselected tabs are recessed on a dark background, and there is a shadow separator line between the tab bar and the content area.
- **Right-click menu**: right-clicking anywhere pops up a top-level menu (always on top, never occluded by the page), with items such as copy/paste.
- **Hover**: moving the mouse (without pressing) updates the coordinates, shown in real time on the key-mouse page.
- **Dropdown**: automatically pops upward near the bottom; the popup scrolls; no "chin" when collapsed.
- **List**: mouse wheel scroll + click to select, the selected row is highlighted.
- **Dragging**: widgets on the stress-test page can be dragged, single-pass drawing without flicker.

## Layout syntax

```c
/* pixel (480 design coordinates, proportionally scaled) */
fx_label_new(pixel("6,36","240,56"), page(5), title("..."));

/* percent (0.0~1.0 float string, relative to parent widget) */
fx_label_new(percent("0.02,0.01","0.98,0.09"), title("fxtk demo · tab page"));
```
