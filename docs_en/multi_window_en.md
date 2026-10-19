# Multi-window backend (FXTK_MW) — summary

Experimental backend in branch `feat/multi-window` (merged to main via PR #12).

- One process, one GL context, N native windows (Win32 WGL / X11 GLX).
- App API: `fxtk_win_create / create_ex(borderless) / show / raise / move / resize / title /
  begin / end / width / height / poll / quit / time / screen / client_offset`.
- Each window has its own offscreen canvas (cached by size); vsync is disabled (serial SwapBuffers).
- Close any window (or Esc) quits the program. Titles are UTF-8.
- Limits: X11 only, primary screen, max 8 windows, input events are not forwarded to widgets yet,
  real HiDPI untested.
- Full design/maintenance doc (Chinese): [../docs/multi-window.md](../docs/multi-window.md).
