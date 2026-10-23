/* ============================================================================
 * fxtk_sokol_mw.c —— fxtk_sokol 的多窗口桌面后端
 *
 * 设计: 一个进程, 一个 GL 上下文, N 个真实原生窗口(系统标题栏)。
 *       sokol_gfx 负责绘制; 每个窗口的默认帧缓冲作为自己的交换链,
 *       逐窗口 glXMakeCurrent / wglMakeCurrent → 画 → SwapBuffers。
 *       应用只看见 fxtk_win.h 的接口, 看不到 sokol。
 * ==========================================================================*/
#include "sokol_gfx.h"
#include "fxtk_mw.h"

/* 由 fxtk_sokol_driver.c 提供: 设置 fxtk 驱动的画布尺寸 (多窗口每帧切换) */
void fxtk_sokol_apply_size(int w, int h);
#include "fxtk_win.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern fx_driver_t fx_sokol_driver;
extern void fxtk_sokol_frame(int fb_w, int fb_h);
extern void fxtk_font_init(const char *font_path, int size);

#define FXTK_MW_MAX 8

/* ================= v2.5.2: 多窗口输入 =================
 * 不直接把原生事件塞进驱动队列 —— 因为一帧里会依次 poll 每扇窗, 直接塞会把 A 窗的事件
 * 带到 B 窗。这里按窗口暂存, 在 b_begin(id) 时(且驱动队列为空时)一次性投递给 fxtk 驱动,
 * 应用随后在该窗口 begin/end 之间调 fx_poll(), 控件层就能像单窗口一样收到鼠标/键盘。 */
#define MW_PEND_MAX 128
enum { MWP_TOUCH = 0, MWP_MOVE = 1, MWP_WHEEL = 2, MWP_KEY = 3, MWP_RCLICK = 4 };
typedef struct { int kind, a, b, c, d; fx_keyev_t key; } mwpend_t;
static mwpend_t s_pend[FXTK_MW_MAX][MW_PEND_MAX];
static int s_pend_n[FXTK_MW_MAX];
static void mw_pend_push(int id, int kind, int a, int b, int c, int d, const fx_keyev_t *key)
{
    if (id < 0 || id >= FXTK_MW_MAX) return;
    if (s_pend_n[id] >= MW_PEND_MAX) return;
    mwpend_t *e = &s_pend[id][s_pend_n[id]++];
    e->kind = kind; e->a = a; e->b = b; e->c = c; e->d = d;
    if (key) e->key = *key;
}
static void mw_pend_flush(int id)
{
    if (id < 0 || id >= FXTK_MW_MAX) return;
    { static int dbg = -1; if (dbg < 0) dbg = getenv("FXTK_MW_DEBUG") ? 1 : 0;
      if (dbg && s_pend_n[id]) fprintf(stderr, "[mw] flush win=%d n=%d\n", id, s_pend_n[id]); }
    /* v2.5.2: 核心 fx_poll 每帧只消费 1 个 touch 事件(if, 不是 while)。若一次投递 press+release,
     * release 会残留到下一扇窗的 fx_poll 里被错误命中 —— 表现为"点了没反应"。
     * 这里每帧最多投 1 个 touch 类事件, 其余留在队里下一帧继续; 键盘/滚轮是 while 消费, 全投。 */
    int touches = 0, keep = 0;
    int blocked = fxtk_sokol_input_touch_pending();   /* 上一帧的 touch 还没被消费: 先投键盘/滚轮/右键 */
    for (int i = 0; i < s_pend_n[id]; i++) {
        mwpend_t *e = &s_pend[id][i];
        int is_touch = (e->kind == MWP_TOUCH || e->kind == MWP_MOVE);
        if (is_touch && (blocked || touches >= 1)) { s_pend[id][keep++] = *e; continue; }
        if (is_touch) touches++;
        if (e->kind == MWP_TOUCH)      fxtk_sokol_input_touch(e->a, e->b, e->c);
        else if (e->kind == MWP_MOVE)  fxtk_sokol_input_touch_move(e->a, e->b, e->c);
        else if (e->kind == MWP_WHEEL) fxtk_sokol_input_wheel(e->a, e->b, e->c);
        else if (e->kind == MWP_KEY)   fxtk_sokol_input_key(&e->key);
        else if (e->kind == MWP_RCLICK) fxtk_sokol_input_right_click(e->a, e->b);
    }
    s_pend_n[id] = keep;
}

#ifdef _WIN32
/* ============================== Windows / WGL ============================== */
#include <windows.h>
#include <GL/gl.h>
static HGLRC s_ctx; static HDC s_dc0; static int s_pf = 0; static PIXELFORMATDESCRIPTOR s_pfd;
static int s_quit = 0;
static int mw_id_of_hwnd(HWND h);   /* 定义在 W[] 之后 */
static void mw_note_size(int id, int w, int h);   /* v2.5.3: WM_SIZE 同步客户区尺寸, 定义在 W[] 之后 */
static LRESULT CALLBACK mw_wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    int id = mw_id_of_hwnd(hw);
    switch (msg) {
    case WM_CLOSE:     s_quit = 1; return 0;        /* 关闭任一窗口 = 退出整个程序 */
    case WM_DESTROY:   PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE) { s_quit = 1; return 0; }
        int mod = ((GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0) | ((GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 0);
        int kc = 0;
        switch (wp) {
        case VK_BACK: kc = FX_KEY_BACKSPACE; break;
        case VK_RETURN: kc = FX_KEY_RETURN; break;
        case VK_LEFT: kc = FX_KEY_LEFT; break;
        case VK_RIGHT: kc = FX_KEY_RIGHT; break;
        case VK_UP: kc = FX_KEY_UP; break;
        case VK_DOWN: kc = FX_KEY_DOWN; break;
        case VK_HOME: kc = FX_KEY_HOME; break;
        case VK_END: kc = FX_KEY_END; break;
        case VK_DELETE: kc = FX_KEY_DELETE; break;
        default: break;
        }
        fx_keyev_t ke; memset(&ke, 0, sizeof ke); ke.down = 1; ke.mod = mod;
        if (kc) { ke.key = kc; mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ke); }
        else if (mod & 1) {   /* Ctrl+C/V/X/A/L 快捷键 (字符输入走 WM_CHAR) */
            char c = 0;
            if (wp >= 'A' && wp <= 'Z') { c = (char)(wp - 'A' + 'a'); if (!strchr("cvxal", c)) c = 0; }
            if (c) { ke.utf8[0] = c; mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ke); }
        }
        return 0;
    }
    case WM_CHAR: {
        if (wp < 32 || (GetKeyState(VK_CONTROL) & 0x8000)) return 0;
        wchar_t wc = (wchar_t)wp; char u8[8]; int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, u8, (int)sizeof u8, NULL, NULL);
        if (n > 0 && n < (int)sizeof u8) {
            fx_keyev_t ke; memset(&ke, 0, sizeof ke); ke.down = 1;
            memcpy(ke.utf8, u8, (size_t)n); ke.utf8[n] = 0;
            mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ke);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        mw_pend_push(id, MWP_MOVE, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), (wp & MK_LBUTTON) ? 1 : 0, 0, NULL);
        return 0;
    case WM_LBUTTONDOWN:
        mw_pend_push(id, MWP_TOUCH, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1, 0, NULL); return 0;
    case WM_LBUTTONUP:
        mw_pend_push(id, MWP_TOUCH, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 0, 0, NULL); return 0;
    case WM_RBUTTONDOWN:
        mw_pend_push(id, MWP_RCLICK, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 0, 0, NULL); return 0;
    case WM_MOUSEWHEEL: {
        POINT pt; pt.x = (int)(short)LOWORD(lp); pt.y = (int)(short)HIWORD(lp);
        ScreenToClient(hw, &pt);
        mw_pend_push(id, MWP_WHEEL, pt.x, pt.y, (int)(short)HIWORD(wp) / 120 * 40, 0, NULL);
        return 0;
    }
    case WM_SIZE:   /* v2.5.3: 用户拖边框/最大化窗口时同步客户区尺寸, 否则仍按旧尺寸绘制 */
        if (wp != SIZE_MINIMIZED) mw_note_size(id, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);   /* 窗口是 Unicode 的, 必须用 W 版默认过程 */
}
typedef struct { HWND hw; HDC dc; int w, h, shown; } mwwin_t;
static mwwin_t W[FXTK_MW_MAX]; static int NW_ = 0, s_cur = -1;
static int s_inited = 0;
static int mw_id_of_hwnd(HWND h) { for (int i = 0; i < NW_; i++) if (W[i].hw == h) return i; return -1; }
static void mw_note_size(int id, int w, int h)
{ if (id >= 0 && id < NW_ && w > 0 && h > 0) { W[id].w = w; W[id].h = h; } }   /* v2.5.3 */

int fxtk_mw_init(void)
{
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = mw_wndproc; wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"fxtk_mw";
    RegisterClassExW(&wc);
    /* 先建一个隐藏窗口来创建 GL 上下文 */
    HWND hw = CreateWindowExW(0, L"fxtk_mw", L"fxtk", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, NULL, NULL, GetModuleHandle(NULL), NULL);
    HDC dc = GetDC(hw);
    PIXELFORMATDESCRIPTOR pfd; memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);
    s_pf = pf; s_pfd = pfd;
    s_ctx = wglCreateContext(dc);
    {   /* 多窗口每个窗口都要 SwapBuffers; vsync 会串行等 6 次垂直同步 → 掉到 ~10fps, 关掉 */
        typedef BOOL (WINAPI *PFN_SWAPINT)(int);
        PFN_SWAPINT pfn=(PFN_SWAPINT)wglGetProcAddress("wglSwapIntervalEXT");
        if(pfn) pfn(0);
    }
    if (!s_ctx) return -1;
    wglMakeCurrent(dc, s_ctx);
    s_dc0 = dc;
    fx_sokol_driver.width = 640; fx_sokol_driver.height = 360;
    fx_sokol_driver.init();
    fxtk_font_init(NULL, 18);
    fx_init(&fx_sokol_driver);
    { extern void fx_backend_set_screen(int,int,float); fx_backend_set_screen(640, 360, 1.0f); }
    s_inited = 1;
    return 0;
}
int fxtk_mw_window_create(const fxtk_mw_desc *d)
{
    if (NW_ >= FXTK_MW_MAX) return -1;
    int i = NW_++;
    int bl = (d->flags & FXTK_MW_BORDERLESS) != 0;
    DWORD style = bl ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, d->w, d->h };
    AdjustWindowRect(&rc, style, FALSE);
    {   wchar_t wt[256];
        int n = MultiByteToWideChar(CP_UTF8, 0, d->title ? d->title : "fxtk", -1, wt, 256);
        if (n <= 0) wcscpy(wt, L"fxtk");
        W[i].hw = CreateWindowExW(bl ? WS_EX_TOPMOST : 0, L"fxtk_mw", wt,
            style, d->x, d->y, rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, GetModuleHandle(NULL), NULL); }
    W[i].dc = GetDC(W[i].hw); W[i].w = d->w; W[i].h = d->h; W[i].shown = 0;
    if (s_pf) SetPixelFormat(W[i].dc, s_pf, &s_pfd);   /* 同一像素格式, 共享 GL 上下文所必需 */
    return i;
}
void fxtk_mw_window_show(int id, int on) { if (id >= 0 && id < NW_) { ShowWindow(W[id].hw, on ? SW_SHOWNOACTIVATE : SW_HIDE); W[id].shown = on; } }
void fxtk_mw_window_raise(int id) { if (id >= 0 && id < NW_) SetWindowPos(W[id].hw, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); }
void fxtk_mw_window_move(int id, int x, int y) { if (id >= 0 && id < NW_) SetWindowPos(W[id].hw, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
void fxtk_mw_window_resize(int id, int w, int h) { if (id >= 0 && id < NW_) { RECT rc = {0,0,w,h}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE); SetWindowPos(W[id].hw, NULL, 0, 0, rc.right-rc.left, rc.bottom-rc.top, SWP_NOMOVE|SWP_NOZORDER); W[id].w = w; W[id].h = h; } }
void fxtk_mw_window_title(int id, const char *t) { if (id >= 0 && id < NW_) SetWindowTextA(W[id].hw, t ? t : "fxtk"); }
void fxtk_mw_window_destroy(int id) { if (id >= 0 && id < NW_) DestroyWindow(W[id].hw); }
void fxtk_mw_make_current(int id) { if (id >= 0 && id < NW_) { wglMakeCurrent(W[id].dc, s_ctx); s_cur = id; } }
void fxtk_mw_present(int id) { if (id >= 0 && id < NW_) SwapBuffers(W[id].dc); }
int  fxtk_mw_width(int id) { return (id >= 0 && id < NW_) ? W[id].w : 0; }
int  fxtk_mw_height(int id) { return (id >= 0 && id < NW_) ? W[id].h : 0; }
sg_swapchain fxtk_mw_swapchain(void)
{
    sg_swapchain sc; memset(&sc, 0, sizeof sc);
    int id = s_cur < 0 ? 0 : s_cur;
    sc.width = fxtk_mw_width(id); sc.height = fxtk_mw_height(id);
    sc.sample_count = 1;
    sc.color_format = SG_PIXELFORMAT_RGBA8;
    sc.depth_format = SG_PIXELFORMAT_NONE;
    sc.gl.framebuffer = 0;
    return sc;
}
sg_environment fxtk_mw_environment(void)
{
    sg_environment env; memset(&env, 0, sizeof env);
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    return env;
}
void fxtk_mw_poll(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { s_quit = 1; break; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
int  fxtk_mw_quit_requested(void) { return s_quit; }
double fxtk_mw_time(void) { return (double)GetTickCount64() / 1000.0; }
void fxtk_mw_client_offset(int id, int *ox, int *oy)
{
    int x=0,y=0;
    if (id >= 0 && id < NW_ && W[id].hw) {
        RECT wr; POINT pt = {0,0};
        GetWindowRect(W[id].hw, &wr);
        ClientToScreen(W[id].hw, &pt);
        x = pt.x - wr.left; y = pt.y - wr.top;
    }
    if (ox) *ox = x; if (oy) *oy = y;
}
void fxtk_mw_screen_size(int *w, int *h)
{
    if (w) *w = GetSystemMetrics(SM_CXSCREEN);
    if (h) *h = GetSystemMetrics(SM_CYSCREEN);
}
int  fxtk_mw_count(void) { return NW_; }
void fxtk_mw_shutdown(void) { if (s_ctx) { wglMakeCurrent(NULL, NULL); wglDeleteContext(s_ctx); s_ctx = 0; } }
int  fxtk_mw_sapp_width(void) { return fxtk_mw_width(s_cur < 0 ? 0 : s_cur); }
int  fxtk_mw_sapp_height(void) { return fxtk_mw_height(s_cur < 0 ? 0 : s_cur); }
void fxtk_mw_sapp_set_title(const char *s) { if (s_cur >= 0) fxtk_mw_window_title(s_cur, s); }
void fxtk_mw_sapp_ime_spot(int x, int y) { (void)x; (void)y; }

#else
/* ============================== Linux / GLX ============================== */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <time.h>
#include <unistd.h>

typedef struct { Window win; int w, h, shown; } mwwin_t;
static Display *dpy; static int scr; static Window s_hidden;
static GLXContext ctx; static GLXFBConfig *fbc; static XVisualInfo *vi;
static Colormap cmap; static Atom wm_delete;
static mwwin_t W[FXTK_MW_MAX]; static int NW_ = 0, s_cur = -1, s_quit = 0, s_inited = 0;
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static int mw_id_of_window(Window w) { for (int i = 0; i < NW_; i++) if (W[i].win == w) return i; return -1; }
static void x11_key_event(int id, XKeyEvent *ke, int mod)
{
    KeySym ks = XLookupKeysym(ke, 0);
    fx_keyev_t ev; memset(&ev, 0, sizeof ev); ev.down = 1; ev.mod = mod;
    int kc = 0;
    switch (ks) {
    case XK_BackSpace: kc = FX_KEY_BACKSPACE; break;
    case XK_Return: case XK_KP_Enter: kc = FX_KEY_RETURN; break;
    case XK_Left: kc = FX_KEY_LEFT; break;
    case XK_Right: kc = FX_KEY_RIGHT; break;
    case XK_Up: kc = FX_KEY_UP; break;
    case XK_Down: kc = FX_KEY_DOWN; break;
    case XK_Home: kc = FX_KEY_HOME; break;
    case XK_End: kc = FX_KEY_END; break;
    case XK_Delete: kc = FX_KEY_DELETE; break;
    default: break;
    }
    if (kc) { ev.key = kc; mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ev); return; }
    if (mod & 1) {
        char c = 0;
        if (ks >= XK_a && ks <= XK_z) { c = (char)ks; if (!strchr("cvxal", c)) c = 0; }
        if (c) { ev.utf8[0] = c; mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ev); }
        return;
    }
    char buf[8]; KeySym dummy;
    int n = XLookupString(ke, buf, (int)sizeof buf, &dummy, NULL);
    if (n > 0 && n < (int)sizeof buf && (unsigned char)buf[0] >= 32) {
        memcpy(ev.utf8, buf, (size_t)n); ev.utf8[n] = 0;
        mw_pend_push(id, MWP_KEY, 0, 0, 0, 0, &ev);
    }
}

int fxtk_mw_init(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "[mw] cannot open display\n"); return -1; }
    scr = DefaultScreen(dpy);
    int gmaj = 0, gmin = 0;
    if (!glXQueryVersion(dpy, &gmaj, &gmin)) { fprintf(stderr, "[mw] no glx\n"); return -1; }
    static int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                           GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 0, None };
    int n = 0;
    fbc = glXChooseFBConfig(dpy, scr, attrs, &n);
    if (!fbc || n < 1) { fprintf(stderr, "[mw] no fbconfig\n"); return -1; }
    vi = glXGetVisualFromFBConfig(dpy, fbc[0]);
    if (!vi) { fprintf(stderr, "[mw] no visual\n"); return -1; }
    cmap = XCreateColormap(dpy, RootWindow(dpy, scr), vi->visual, AllocNone);
    typedef GLXContext (*create_proc)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
    create_proc cp = (create_proc)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
    if (cp) {
        int ca[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                     GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
        ctx = cp(dpy, fbc[0], NULL, True, ca);
    }
    if (!ctx) ctx = glXCreateNewContext(dpy, fbc[0], GLX_RGBA_TYPE, NULL, True);
    if (!ctx) { fprintf(stderr, "[mw] no gl context\n"); return -1; }
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    /* 建一个 1x1 隐藏窗口承载上下文, 然后初始化 fxtk 驱动 */
    { XSetWindowAttributes swa; memset(&swa, 0, sizeof swa);
      swa.colormap = cmap; swa.border_pixel = 0; swa.event_mask = 0;
      Window w = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, 1, 1, 0, vi->depth, InputOutput,
                               vi->visual, CWBorderPixel | CWColormap | CWEventMask, &swa);
      XStoreName(dpy, w, "fxtk-mw-main");
      s_hidden = w; }
    if (!glXMakeCurrent(dpy, s_hidden, ctx)) { fprintf(stderr, "[mw] make current failed\n"); return -1; }
    {   /* 关 vsync: 多窗口串行 Swap 会互相等垂直同步 */
        typedef void (*PFN_MESA)(int);
        PFN_MESA pm=(PFN_MESA)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
        if(pm){ pm(0); }
        else { typedef void (*PFN_EXT)(Display*,GLXDrawable,int);
               PFN_EXT pe=(PFN_EXT)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
               if(pe) pe(dpy,s_hidden,0); }
    }
    fx_sokol_driver.width = 640; fx_sokol_driver.height = 360;
    fx_sokol_driver.init();
    fxtk_font_init(NULL, 18);
    fx_init(&fx_sokol_driver);
    { extern void fx_backend_set_screen(int, int, float); fx_backend_set_screen(640, 360, 1.0f); }
    s_inited = 1;
    return 0;
}
static void x11_set_title(Window w, const char *t)
{
    XStoreName(dpy, w, t);
    Atom net = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    XChangeProperty(dpy, w, net, utf8, 8, PropModeReplace, (const unsigned char*)t, (int)strlen(t));
}
int fxtk_mw_window_create(const fxtk_mw_desc *d)
{
    if (NW_ >= FXTK_MW_MAX) return -1;
    int i = NW_++;
    XSetWindowAttributes swa; memset(&swa, 0, sizeof swa);
    swa.colormap = cmap; swa.background_pixel = 0; swa.border_pixel = 0;
    swa.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | KeyPressMask | KeyReleaseMask | FocusChangeMask;
    swa.override_redirect = (d->flags & FXTK_MW_BORDERLESS) ? True : False;
    Window w = XCreateWindow(dpy, RootWindow(dpy, scr), d->x, d->y, (unsigned)d->w, (unsigned)d->h,
        0, vi->depth, InputOutput, vi->visual,
        CWBorderPixel | CWColormap | CWEventMask | CWBackPixel | CWOverrideRedirect, &swa);
    x11_set_title(w, d->title ? d->title : "fxtk");
    XSetWMProtocols(dpy, w, &wm_delete, 1);
    W[i].win = w; W[i].w = d->w; W[i].h = d->h; W[i].shown = 0;
    if (!(d->flags & FXTK_MW_BORDERLESS)) XMapWindow(dpy, w);   /* 无边框窗(闪白)等 show 再映射 */
    if (s_cur < 0) { glXMakeCurrent(dpy, w, ctx); s_cur = i; }
    XFlush(dpy);
    return i;
}
void fxtk_mw_window_show(int id, int on) { if (id < 0 || id >= NW_) return; if (on) { XMapWindow(dpy, W[id].win); XRaiseWindow(dpy, W[id].win); } else XUnmapWindow(dpy, W[id].win); W[id].shown = on; XFlush(dpy); }
void fxtk_mw_window_raise(int id) { if (id >= 0 && id < NW_) { XRaiseWindow(dpy, W[id].win); XFlush(dpy); } }
void fxtk_mw_window_move(int id, int x, int y) { if (id >= 0 && id < NW_) XMoveWindow(dpy, W[id].win, x, y); }
void fxtk_mw_window_resize(int id, int w, int h) { if (id >= 0 && id < NW_) { XResizeWindow(dpy, W[id].win, (unsigned)w, (unsigned)h); W[id].w = w; W[id].h = h; } }
void fxtk_mw_window_title(int id, const char *t) { if (id >= 0 && id < NW_) { XStoreName(dpy, W[id].win, t ? t : "fxtk"); XFlush(dpy); } }
void fxtk_mw_window_destroy(int id) { if (id >= 0 && id < NW_) XDestroyWindow(dpy, W[id].win); }
void fxtk_mw_make_current(int id) { if (id < 0 || id >= NW_) return; glXMakeCurrent(dpy, W[id].win, ctx); s_cur = id; }
void fxtk_mw_present(int id) { if (id >= 0 && id < NW_) glXSwapBuffers(dpy, W[id].win); }
int fxtk_mw_width(int id) { return (id >= 0 && id < NW_) ? W[id].w : 0; }
int fxtk_mw_height(int id) { return (id >= 0 && id < NW_) ? W[id].h : 0; }
sg_swapchain fxtk_mw_swapchain(void)
{
    sg_swapchain sc; memset(&sc, 0, sizeof sc);
    int id = s_cur < 0 ? 0 : s_cur;
    sc.width = fxtk_mw_width(id); sc.height = fxtk_mw_height(id);
    sc.sample_count = 1;
    sc.color_format = SG_PIXELFORMAT_RGBA8;
    sc.depth_format = SG_PIXELFORMAT_NONE;
    sc.gl.framebuffer = 0;
    return sc;
}
sg_environment fxtk_mw_environment(void)
{
    sg_environment env; memset(&env, 0, sizeof env);
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    return env;
}
void fxtk_mw_poll(void)
{
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("FXTK_MW_DEBUG") ? 1 : 0;
    while (XPending(dpy)) {
        XEvent e; XNextEvent(dpy, &e);
        if (dbg) fprintf(stderr, "[mw] ev type=%d win=%lu\n", e.type, (unsigned long)e.xany.window);
        if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wm_delete) { s_quit = 1; continue; }
        if (e.type == DestroyNotify) { s_quit = 1; continue; }
        int id = mw_id_of_window(e.xany.window);
        if (id < 0) continue;
        int mod = ((e.xkey.state & ControlMask) ? 1 : 0) | ((e.xkey.state & ShiftMask) ? 2 : 0);
        switch (e.type) {
        case KeyPress:
            if (XLookupKeysym(&e.xkey, 0) == XK_Escape) { s_quit = 1; break; }
            x11_key_event(id, &e.xkey, mod);
            break;
        case ButtonPress:
            if (e.xbutton.button == Button3) mw_pend_push(id, MWP_RCLICK, e.xbutton.x, e.xbutton.y, 0, 0, NULL);
            else if (e.xbutton.button == Button4 || e.xbutton.button == Button5)
                mw_pend_push(id, MWP_WHEEL, e.xbutton.x, e.xbutton.y, e.xbutton.button == Button4 ? 40 : -40, 0, NULL);
            else if (e.xbutton.button == Button1) mw_pend_push(id, MWP_TOUCH, e.xbutton.x, e.xbutton.y, 1, 0, NULL);
            break;
        case ButtonRelease:
            if (e.xbutton.button == Button1) mw_pend_push(id, MWP_TOUCH, e.xbutton.x, e.xbutton.y, 0, 0, NULL);
            break;
        case MotionNotify:
            mw_pend_push(id, MWP_MOVE, e.xmotion.x, e.xmotion.y, (e.xmotion.state & Button1Mask) ? 1 : 0, 0, NULL);
            break;
        case ConfigureNotify:   /* v2.5.3: 外部缩放/最大化窗口时同步客户区尺寸 */
            if (e.xconfigure.width > 0 && e.xconfigure.height > 0 &&
                (e.xconfigure.width != W[id].w || e.xconfigure.height != W[id].h)) {
                W[id].w = e.xconfigure.width;
                W[id].h = e.xconfigure.height;
            }
            break;
        default: break;
        }
    }
}
int fxtk_mw_quit_requested(void) { return s_quit; }
void fxtk_mw_client_offset(int id, int *ox, int *oy)
{
    /* X11: 带 WM 时 XMoveWindow 传的就是客户区位置(WM 自己加框架),
     * 这里返回 0; 只有 Win32 需要扣掉标题栏高度. */
    (void)id;
    if (ox) *ox = 0; if (oy) *oy = 0;
}
void fxtk_mw_screen_size(int *w, int *h)
{
    if (w) *w = DisplayWidth(dpy, DefaultScreen(dpy));
    if (h) *h = DisplayHeight(dpy, DefaultScreen(dpy));
}
double fxtk_mw_time(void) { return now_s(); }
int fxtk_mw_count(void) { return NW_; }
void fxtk_mw_shutdown(void) { if (dpy) { glXMakeCurrent(dpy, None, NULL); glXDestroyContext(dpy, ctx); XCloseDisplay(dpy); dpy = 0; } }
int fxtk_mw_sapp_width(void) { return fxtk_mw_width(s_cur < 0 ? 0 : s_cur); }
int fxtk_mw_sapp_height(void) { return fxtk_mw_height(s_cur < 0 ? 0 : s_cur); }
void fxtk_mw_sapp_set_title(const char *s) { if (s_cur >= 0) fxtk_mw_window_title(s_cur, s); }
void fxtk_mw_sapp_ime_spot(int x, int y) { (void)x; (void)y; }
#endif

/* ============================== fxtk_win 后端 vtable ============================== */
static int  b_init(void) { return fxtk_mw_init(); }
static void b_shutdown(void) { fxtk_mw_shutdown(); }
static int  b_count(void) { return fxtk_mw_count(); }
static int  b_create(int w, int h, int x, int y, const char *title)
{
    fxtk_mw_desc d = { title, x, y, w, h };
    return fxtk_mw_window_create(&d);
}
static void b_begin(int id)
{
    fxtk_mw_make_current(id);
    /* 关键: 在该窗口绘制前就把驱动的屏幕尺寸切过来,
     * 否则文本纹理槽/像素层仍按上一个窗口的尺寸分配 → 文字错位/花屏 */
    fxtk_sokol_apply_size(fxtk_mw_width(id), fxtk_mw_height(id));
    /* v2.5.2: 把本窗口暂存的输入投递给控件层 —— 键盘/滚轮/右键总是投, touch 类
     * 若上一帧还有残留则留在本窗口队里(防串窗), 不阻塞其它类型(否则连续 mousemove 会让键盘饿死) */
    mw_pend_flush(id);
}
static void b_end(int id)
{
    int w = fxtk_mw_width(id), h = fxtk_mw_height(id);
    fxtk_sokol_frame(w, h);      /* 驱动: 画布合成 + swapchain pass + sg_commit */
    fxtk_mw_present(id);
}
static int  b_create_ex(int w, int h, int x, int y, const char *title, unsigned flags)
{
    fxtk_mw_desc d = { title, x, y, w, h, flags };
    return fxtk_mw_window_create(&d);
}
static const fxtk_win_backend s_backend = {
    "fxtk_sokol_mw", b_init, b_shutdown, b_count, b_create,
    fxtk_mw_window_destroy, fxtk_mw_window_show, fxtk_mw_window_move,
    fxtk_mw_window_resize, fxtk_mw_window_title, b_begin, b_end,
    fxtk_mw_width, fxtk_mw_height, fxtk_mw_poll, fxtk_mw_quit_requested, fxtk_mw_time,
    fxtk_mw_window_raise, b_create_ex, fxtk_mw_screen_size, fxtk_mw_client_offset
};
void fxtk_mw_register(void) { fxtk_win_backend_set(&s_backend); }
