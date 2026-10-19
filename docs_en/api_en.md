# fxtk API (English summary)

The complete, per-function API reference (signatures + usage + semantics) is maintained in
**[docs/api.md](../docs/api.md)** (Chinese; CI enforces that every public function from the headers appears there).

Quick orientation:

- Drawing/primitives, text, canvas, transforms, images: sections 7-10 of docs/api.md.
- Widgets/attributes/layout/input: sections 2-6.
- Backend services (fs/image/clipboard/prefs/dialogs): section 15; also [backends.md](../docs/backends.md).
- Desktop / single-window `fx_win_*`: section 16; also [desktop.md](../docs/desktop.md).
- Real multi-window backend (`fxtk_win_*`, `-DFXTK_MW`): section 18 + [docs/multi-window.md](../docs/multi-window.md) + [multi_window_en.md](multi_window_en.md).
- In-canvas window manager (`fx_wm_*`): section 19 + [docs/window-manager.md](../docs/window-manager.md) + [window_manager_en.md](window_manager_en.md).
- Driver vtable `fx_driver_t`: section 20 + [internals.md](../docs/internals.md).
