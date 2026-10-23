# Multi-window backend (FXTK_MW) — summary

Experimental backend in branch `feat/multi-window` (merged to main via PR #12).

- One process, one GL context, N native windows (Win32 WGL / X11 GLX).
- App API: `fxtk_win_create / create_ex(borderless) / show / raise / move / resize / title /
  begin / end / width / height / poll / quit / time / screen / client_offset`.
- Each window has its own offscreen canvas/framebuffer (cached by size, LRU-evicted when full);
  vsync is disabled (serial SwapBuffers).
- Mouse/keyboard input is delivered to the widget tree of the focused window (v2.5.2).
- Close any window (or Esc) quits the program. Titles are UTF-8.
- External resizes (WM drag / maximize) are picked up via ConfigureNotify (X11) / WM_SIZE (Win32) since
  v2.5.3: the backend syncs the client size and re-lays out / repaints on the next frame.
- Limits: X11 only, primary screen, max 8 windows, no clipboard/IME in the MW path, real HiDPI untested.
- Full design/maintenance doc (Chinese): [../docs/multi-window.md](../docs/multi-window.md).
