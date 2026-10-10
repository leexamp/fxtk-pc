/**
 * fxtk_window.c — v2.5: 单窗口桌面行为 (去标题栏 / 置顶 / 移动 / 居中 / 拖动)
 *
 * 设计说明见 fxtk_desktop.h 的「支持矩阵」。这里只强调三点容易踩的坑:
 *
 * ① 【为什么用 uint32_t 而不是 unsigned long 写 MotifHints】
 *    MotifWmHints 的经典写法是 5 个 unsigned long。在 LP64(Linux/macOS 64 位)上
 *    long 是 8 字节, 而 XChangeProperty(format=32) 会按【连续的 32 位单元】去读这 20 个单元 ——
 *    于是它读到的是 flags 的低32位 / flags 的高32位 / functions 的低32位 ……
 *    整份 hints 全部错位, 表现是"设了跟没设一样"(甚至更糟: decorations 被解读成乱值)。
 *    这里改用 5 个 uint32_t, 结构体恰好 20 字节, 与 format=32 × 5 严格对应。
 *
 * ② 【置顶必须发 ClientMessage 给 root, 不能只设属性】
 *    窗口已 map 之后再 XChangeProperty(_NET_WM_STATE) 是无效的 —— EWMH 规定运行中改状态
 *    要发 _NET_WM_STATE ClientMessage 给 root。只设属性只在"窗口 map 之前"有效。
 *
 * ③ 【去标题栏在某些 WM 上要 remap 才生效】
 *    部分 WM 只在窗口 map/unmap 时读 _MOTIF_WM_HINTS。这里【不做】 unmap/map,
 *    因为那会闪一下并可能丢焦点; 代价是这部分 WM 上要等下次 remap 才生效。
 *    宁可少生效, 也不做会产生闪烁的副作用 —— 与项目里"止闪"的一贯取舍一致。
 */
#include "fxtk_desktop.h"
#include "fxtk_sokol.h"
#include "sokol_app.h"
#include <string.h>

#if defined(_WIN32)
  #define FXTK_WIN_WIN32 1
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_X11)
  #define FXTK_WIN_X11 1
  #include <X11/Xlib.h>
  #include <X11/Xatom.h>
#elif defined(__linux__) && !defined(__ANDROID__)
  /* sokol 在 Linux 上走 X11; 未定义 SOKOL_X11 时仍按 X11 处理(与 sokol_app.h 默认一致) */
  #define FXTK_WIN_X11 1
  #include <X11/Xlib.h>
  #include <X11/Xatom.h>
#else
  #define FXTK_WIN_NONE 1
#endif

/* 我们【请求过】的状态。注意: 这是请求记录, 不是屏幕实况 —— 见头文件里的诚实声明。 */
static int s_borderless = 0;
static int s_ontop      = 0;

/* ============================ X11 ============================ */
#if defined(FXTK_WIN_X11)

static Display *x_dpy(void) { return (Display *)sapp_x11_get_display(); }
static Window   x_win(void) { return (Window)sapp_x11_get_window(); }

int fx_win_available(void)
{
    Display *d = x_dpy();
    Window   w = x_win();
    return (d != NULL && w != 0) ? 1 : 0;
}

int fx_win_set_borderless(int on)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return 0;

    Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", False);
    if (a == None) return 0;

    /* 5 × 32bit = 20B, 与 format=32 × 5 严格对应(见文件头 ①) */
    uint32_t hints[5];
    memset(hints, 0, sizeof hints);
    hints[0] = 2;                        /* MWM_HINTS_DECORATIONS */
    hints[2] = (uint32_t)(on ? 0 : 1);   /* decorations: 0=无边框 1=正常 */
    XChangeProperty(d, w, a, a, 32, PropModeReplace, (unsigned char *)hints, 5);
    XFlush(d);
    s_borderless = on ? 1 : 0;
    return 1;
}

int fx_win_set_always_on_top(int on)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return 0;

    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type         = ClientMessage;
    e.xclient.window       = w;
    e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = on ? 1 : 0;   /* 0=remove 1=add 2=toggle */
    e.xclient.data.l[1]    = (long)XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
    e.xclient.data.l[2]    = 0;
    e.xclient.data.l[3]    = 1;            /* source indication: 普通应用 */
    XSendEvent(d, DefaultRootWindow(d), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(d);
    s_ontop = on ? 1 : 0;
    return 1;
}

int fx_win_move(int x, int y)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return 0;
    XMoveWindow(d, w, x, y);
    XFlush(d);
    return 1;
}

int fx_win_get_pos(int *x, int *y)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return 0;

    int rx = 0, ry = 0;
    Window child = 0;
    if (!XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &rx, &ry, &child))
        return 0;
    if (x) *x = rx;
    if (y) *y = ry;
    return 1;
}

int fx_win_center(void)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return 0;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, w, &wa)) return 0;
    /* 注意: DisplayWidth(d, n) 的 n 是【屏幕索引】, 不能传 wa.screen(那是 Screen*);
     * 要按 Screen* 取值应使用 WidthOfScreen/HeightOfScreen(无 X 前缀 —— 编译器
     * 在报错时建议的 XWidthOfScreen 并不存在, 别被它带偏)。 */
    int sw = WidthOfScreen(wa.screen);
    int sh = HeightOfScreen(wa.screen);
    int x = (sw - wa.width)  / 2;
    int y = (sh - wa.height) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return fx_win_move(x, y);
}

void fx_win_drag_begin(void)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return;

    Window root = DefaultRootWindow(d);
    /* _NET_WM_MOVERESIZE 的坐标必须是【根窗口坐标的当前指针位置】 */
    Window rr = 0, child = 0;
    int px = 0, py = 0, wx = 0, wy = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(d, root, &rr, &child, &px, &py, &wx, &wy, &mask)) return;

    XUngrabPointer(d, CurrentTime);   /* EWMH 要求先释放指针抓取, 否则 WM 不接管 */

    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type         = ClientMessage;
    e.xclient.window       = w;
    e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False);
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = px;
    e.xclient.data.l[1]    = py;
    e.xclient.data.l[2]    = 8;   /* _NET_WM_MOVERESIZE_MOVE */
    e.xclient.data.l[3]    = 1;   /* Button1 */
    e.xclient.data.l[4]    = 1;
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(d);
}

int fx_win_get_decorated(void)
{
    Display *d = x_dpy();
    Window   w = x_win();
    if (!d || !w) return -1;

    Atom a = XInternAtom(d, "_NET_FRAME_EXTENTS", False), type;
    int fmt;
    unsigned long n, after;
    unsigned char *p = NULL;
    if (XGetWindowProperty(d, w, a, 0, 4, False, AnyPropertyType,
                           &type, &fmt, &n, &after, &p) != Success || !p)
        return -1;                       /* 无 WM / 非 EWMH / 未托管 → 问不出来 */
    long left = -1, top = -1;
    if (n >= 4) { left = ((long *)p)[0]; top = ((long *)p)[2]; }
    XFree(p);
    if (left < 0 || top < 0) return -1;
    return (left == 0 && top == 0) ? 0 : 1;
}

/* ============================ Win32 ============================ */
#elif defined(FXTK_WIN_WIN32)

static HWND w32_hwnd(void) { return (HWND)sapp_win32_get_hwnd(); }

int fx_win_available(void) { return w32_hwnd() != NULL ? 1 : 0; }

int fx_win_set_borderless(int on)
{
    HWND h = w32_hwnd();
    if (!h) return 0;

    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    if (on) {
        st &= ~(LONG_PTR)(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                          WS_MAXIMIZEBOX | WS_SYSMENU);
        st |=  (LONG_PTR)(WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS);
    } else {
        st &= ~(LONG_PTR)(WS_POPUP);
        st |=  (LONG_PTR)(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                          WS_MAXIMIZEBOX | WS_SYSMENU | WS_VISIBLE | WS_CLIPSIBLINGS);
    }
    SetWindowLongPtrW(h, GWL_STYLE, st);
    /* SWP_FRAMECHANGED 才会让新样式立即重算边框 —— 漏了它样式不生效 */
    SetWindowPos(h, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    s_borderless = on ? 1 : 0;
    return 1;
}

int fx_win_set_always_on_top(int on)
{
    HWND h = w32_hwnd();
    if (!h) return 0;
    SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
    s_ontop = on ? 1 : 0;
    return 1;
}

int fx_win_move(int x, int y)
{
    HWND h = w32_hwnd();
    if (!h) return 0;
    SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return 1;
}

int fx_win_get_pos(int *x, int *y)
{
    HWND h = w32_hwnd();
    if (!h) return 0;
    RECT r;
    if (!GetWindowRect(h, &r)) return 0;
    if (x) *x = (int)r.left;
    if (y) *y = (int)r.top;
    return 1;
}

int fx_win_center(void)
{
    HWND h = w32_hwnd();
    if (!h) return 0;
    RECT r;
    if (!GetWindowRect(h, &r)) return 0;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - (int)(r.right - r.left)) / 2;
    int y = (sh - (int)(r.bottom - r.top)) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return fx_win_move(x, y);
}

void fx_win_drag_begin(void)
{
    HWND h = w32_hwnd();
    if (!h) return;
    /* 经典手法: 把这次鼠标按下伪装成"点在标题栏上", 交给系统进入拖动循环 */
    ReleaseCapture();
    SendMessageW(h, WM_NCLBUTTONDOWN, (WPARAM)HTCAPTION, 0);
}

int fx_win_get_decorated(void)
{
    HWND h = w32_hwnd();
    if (!h) return -1;
    return (GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) ? 1 : 0;
}

/* ============================ 其它平台 ============================ */
#else

int  fx_win_available(void)                 { return 0; }
int  fx_win_set_borderless(int on)          { (void)on; return 0; }
int  fx_win_set_always_on_top(int on)       { (void)on; return 0; }
int  fx_win_move(int x, int y)              { (void)x; (void)y; return 0; }
int  fx_win_get_pos(int *x, int *y)         { (void)x; (void)y; return 0; }
int  fx_win_center(void)                    { return 0; }
void fx_win_drag_begin(void)                { }
int  fx_win_get_decorated(void)             { return -1; }

#endif

/* 与平台无关的查询: 返回我们【请求过】的状态(非屏幕实况) */
int fx_win_get_borderless(void) { return s_borderless; }
int fx_win_get_always_on_top(void) { return s_ontop; }
