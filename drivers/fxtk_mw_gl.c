/* fxtk_mw_gl.c — 多窗口 GL 后端实现 (X11 / Win32 WGL) */
#include "fxtk_mw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FXTK_MW_MAX 8

#ifdef _WIN32
/* ============================ Windows / WGL ============================ */
#include <windows.h>
#include <GL/gl.h>
static HGLRC s_ctx; static HDC s_dc0; static HWND s_hw0;
typedef struct { HWND hw; HDC dc; int w,h,shown; } mwwin_t;
static mwwin_t W[FXTK_MW_MAX]; static int NW_;
static int s_cur=-1;
static double now_s(void){ return (double)GetTickCount64()/1000.0; }
int fxtk_mw_init(void){
    WNDCLASSA wc; memset(&wc,0,sizeof wc);
    wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(NULL);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.lpszClassName="fxtk_mw";
    RegisterClassA(&wc);
    return 0;
}
int fxtk_mw_window_create(const fxtk_mw_desc *d){
    if(NW_>=FXTK_MW_MAX) return -1;
    int i=NW_++;
    RECT rc={0,0,d->w,d->h}; AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    W[i].hw=CreateWindowExA(0,"fxtk_mw",d->title?d->title:"fxtk",
        WS_OVERLAPPEDWINDOW,d->x,d->y,rc.right-rc.left,rc.bottom-rc.top,NULL,NULL,GetModuleHandle(NULL),NULL);
    W[i].dc=GetDC(W[i].hw); W[i].w=d->w; W[i].h=d->h; W[i].shown=0;
    if(!s_ctx){
        PIXELFORMATDESCRIPTOR pfd; memset(&pfd,0,sizeof pfd); pfd.nSize=sizeof pfd; pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=0; pfd.iLayerType=PFD_MAIN_PLANE;
        int pf=ChoosePixelFormat(W[i].dc,&pfd); SetPixelFormat(W[i].dc,pf,&pfd);
        s_ctx=wglCreateContext(W[i].dc); wglMakeCurrent(W[i].dc,s_ctx); s_cur=i;
    }
    return i;
}
void fxtk_mw_window_show(int id,int on){ if(id<0||id>=NW_)return; ShowWindow(W[id].hw,on?SW_SHOWNOACTIVATE:SW_HIDE); W[id].shown=on; }
void fxtk_mw_window_move(int id,int x,int y){ if(id<0||id>=NW_)return; SetWindowPos(W[id].hw,NULL,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); }
void fxtk_mw_window_resize(int id,int w,int h){ if(id<0||id>=NW_)return; RECT rc={0,0,w,h}; AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE); SetWindowPos(W[id].hw,NULL,0,0,rc.right-rc.left,rc.bottom-rc.top,SWP_NOMOVE|SWP_NOZORDER); W[id].w=w; W[id].h=h; }
void fxtk_mw_window_title(int id,const char*t){ if(id>=0&&id<NW_) SetWindowTextA(W[id].hw,t?t:"fxtk"); }
void fxtk_mw_window_destroy(int id){ if(id>=0&&id<NW_){ DestroyWindow(W[id].hw); } }
void fxtk_mw_make_current(int id){ if(id<0||id>=NW_||!s_ctx)return; wglMakeCurrent(W[id].dc,s_ctx); s_cur=id; }
void fxtk_mw_present(int id){ if(id<0||id>=NW_)return; SwapBuffers(W[id].dc); }
int fxtk_mw_width(int id){ return (id>=0&&id<NW_)?W[id].w:0; }
int fxtk_mw_height(int id){ return (id>=0&&id<NW_)?W[id].h:0; }
sg_swapchain fxtk_mw_swapchain(int id){
    sg_swapchain sc; memset(&sc,0,sizeof sc);
    sc.width=fxtk_mw_width(id); sc.height=fxtk_mw_height(id);
    sc.sample_count=1; sc.color_format=SG_PIXELFORMAT_RGBA8; sc.depth_format=SG_PIXELFORMAT_NONE;
    return sc;
}
void fxtk_mw_poll(void){ MSG m; while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageA(&m); } }
double fxtk_mw_time(void){ return now_s(); }
int fxtk_mw_count(void){ return NW_; }
int fxtk_mw_quit_requested(void){ return 0; }
int fxtk_mw_sapp_width(void){ return fxtk_mw_width(s_cur<0?0:s_cur); }
int fxtk_mw_sapp_height(void){ return fxtk_mw_height(s_cur<0?0:s_cur); }
void fxtk_mw_sapp_set_title(const char*s){ if(s_cur>=0) fxtk_mw_window_title(s_cur,s); }
void fxtk_mw_sapp_ime_spot(int x,int y){ (void)x;(void)y; }
void fxtk_mw_sapp_request_quit(void){}
#else
/* ============================ Linux / GLX ============================ */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <time.h>
#include <unistd.h>

typedef struct { Window win; int w,h,shown; } mwwin_t;
static Display *dpy; static int scr;
static GLXContext ctx; static GLXFBConfig *fbc=0; static XVisualInfo *vi=0;
static Colormap cmap;
static mwwin_t W[FXTK_MW_MAX]; static int NW_=0, s_cur=-1;
static Atom wm_delete; static int s_quit=0;
static double now_s(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec*1e-9; }

int fxtk_mw_init(void){
    dpy = XOpenDisplay(NULL);
    if(!dpy){ fprintf(stderr,"[mw] cannot open display\n"); return -1; }
    scr = DefaultScreen(dpy);
    int glx_major=0, glx_minor=0;
    if(!glXQueryVersion(dpy,&glx_major,&glx_minor)){ fprintf(stderr,"[mw] no glx\n"); return -1; }
    static int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE,8, GLX_GREEN_SIZE,8,
                           GLX_BLUE_SIZE,8, GLX_ALPHA_SIZE,8, GLX_DEPTH_SIZE,0, None };
    int n=0;
    fbc = glXChooseFBConfig(dpy,scr,attrs,&n);
    if(!fbc || n<1){ fprintf(stderr,"[mw] no fbconfig\n"); return -1; }
    vi = glXGetVisualFromFBConfig(dpy, fbc[0]);
    if(!vi){ fprintf(stderr,"[mw] no visual\n"); return -1; }
    cmap = XCreateColormap(dpy, RootWindow(dpy,scr), vi->visual, AllocNone);
    /* GL 3.3 core */
    typedef GLXContext (*create_proc)(Display*,GLXFBConfig,GLXContext,Bool,const int*);
    create_proc cp = (create_proc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if(cp){
        int ca[] = { GLX_CONTEXT_MAJOR_VERSION_ARB,3, GLX_CONTEXT_MINOR_VERSION_ARB,3,
                     GLX_CONTEXT_PROFILE_MASK_ARB,GLX_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
        ctx = cp(dpy,fbc[0],NULL,True,ca);
    }
    if(!ctx) ctx = glXCreateNewContext(dpy,fbc[0],GLX_RGBA_TYPE,NULL,True);
    if(!ctx){ fprintf(stderr,"[mw] no gl context\n"); return -1; }
    wm_delete = XInternAtom(dpy,"WM_DELETE_WINDOW",False);
    return 0;
}
int fxtk_mw_window_create(const fxtk_mw_desc *d){
    if(NW_>=FXTK_MW_MAX) return -1;
    int i=NW_++;
    XSetWindowAttributes swa; memset(&swa,0,sizeof swa);
    swa.colormap = cmap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.event_mask = ExposureMask|StructureNotifyMask;
    Window w = XCreateWindow(dpy, RootWindow(dpy,scr), d->x,d->y,(unsigned)d->w,(unsigned)d->h,
        0, vi->depth, InputOutput, vi->visual, CWBorderPixel|CWColormap|CWEventMask|CWBackPixel, &swa);
    XStoreName(dpy,w,d->title?d->title:"fxtk");
    XSetWMProtocols(dpy,w,&wm_delete,1);
    W[i].win=w; W[i].w=d->w; W[i].h=d->h; W[i].shown=0;
    return i;
}
void fxtk_mw_window_show(int id,int on){ if(id<0||id>=NW_)return; if(on){XMapWindow(dpy,W[id].win);XRaiseWindow(dpy,W[id].win);} else XUnmapWindow(dpy,W[id].win); W[id].shown=on; XFlush(dpy);}
void fxtk_mw_window_move(int id,int x,int y){ if(id<0||id>=NW_)return; XMoveWindow(dpy,W[id].win,x,y); }
void fxtk_mw_window_resize(int id,int w,int h){ if(id<0||id>=NW_)return; XResizeWindow(dpy,W[id].win,(unsigned)w,(unsigned)h); W[id].w=w; W[id].h=h; }
void fxtk_mw_window_title(int id,const char*t){ if(id>=0&&id<NW_){ XStoreName(dpy,W[id].win,t?t:"fxtk"); XFlush(dpy);} }
void fxtk_mw_window_destroy(int id){ if(id>=0&&id<NW_) XDestroyWindow(dpy,W[id].win); }
void fxtk_mw_make_current(int id){ if(id<0||id>=NW_)return; glXMakeCurrent(dpy,W[id].win,ctx); s_cur=id; }
void fxtk_mw_present(int id){ if(id<0||id>=NW_)return; glXSwapBuffers(dpy,W[id].win); }
int fxtk_mw_width(int id){ return (id>=0&&id<NW_)?W[id].w:0; }
int fxtk_mw_height(int id){ return (id>=0&&id<NW_)?W[id].h:0; }
sg_swapchain fxtk_mw_swapchain(int id){
    sg_swapchain sc; memset(&sc,0,sizeof sc);
    sc.width=fxtk_mw_width(id); sc.height=fxtk_mw_height(id);
    sc.sample_count=1; sc.color_format=SG_PIXELFORMAT_RGBA8; sc.depth_format=SG_PIXELFORMAT_NONE;
    sc.gl.framebuffer=0;
    return sc;
}
static sg_swapchain s_cur_swap;
void fxtk_mw_swapchain_set(int id){ s_cur_swap=fxtk_mw_swapchain(id); }
sg_swapchain fxtk_mw_swapchain_cur(void){ return s_cur_swap; }
void fxtk_mw_poll(void){
    while(XPending(dpy)){
        XEvent e; XNextEvent(dpy,&e);
        if(e.type==ClientMessage && (Atom)e.xclient.data.l[0]==wm_delete) s_quit=1;
        if(e.type==ConfigureNotify && e.xconfigure.window==(s_cur>=0?W[s_cur].win:0)){
            W[s_cur].w=e.xconfigure.width; W[s_cur].h=e.xconfigure.height;
        }
    }
}
int fxtk_mw_quit_requested(void){ return s_quit; }
double fxtk_mw_time(void){ return now_s(); }
int fxtk_mw_count(void){ return NW_; }
int fxtk_mw_sapp_width(void){ return fxtk_mw_width(s_cur<0?0:s_cur); }
int fxtk_mw_sapp_height(void){ return fxtk_mw_height(s_cur<0?0:s_cur); }
void fxtk_mw_sapp_set_title(const char*s){ if(s_cur>=0) fxtk_mw_window_title(s_cur,s); }
void fxtk_mw_sapp_ime_spot(int x,int y){ (void)x;(void)y; }
void fxtk_mw_sapp_request_quit(void){ s_quit=1; }
void fxtk_mw_shutdown(void){ if(dpy){ glXMakeCurrent(dpy,None,NULL); glXDestroyContext(dpy,ctx); XCloseDisplay(dpy); dpy=0; } }
#endif
