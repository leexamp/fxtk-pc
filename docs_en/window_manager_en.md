# In-canvas window manager (fxtk_wm) — summary

`fxtk_wm` draws "desktop-like windows" **inside a single fxtk window/canvas**: each window's content is
rendered to an offscreen canvas and composited by `fx_wm_draw()` with title bars, borders, z-order and focus.
It is not the real multi-window backend (`FXTK_MW`, see [multi-window.md](../docs/multi-window.md)).

- Create: `fx_wm_add(title, style, cw, ch)`; styles: `FX_WM_LINUX / WIN11 / TERM / PLAIN`.
- Per frame: `fx_wm_set_ui(scale)` (optional) → `fx_wm_prepare(fn,ud)` → draw background →
  `fx_wm_draw(fn,ud)`; `fn(idx,cw,ch,ud)` renders one window's content.
- Properties: position/size/z/focus/visible/title/style; `fx_wm_set_rot()` is not implemented yet.
- Limits: no OS window behavior (no taskbar/minimize/focus), offscreen canvases cost memory;
  use the real multi-window backend when you need actual system windows.
- Full doc (Chinese): [docs/window-manager.md](../docs/window-manager.md).
