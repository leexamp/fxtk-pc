#ifdef _WIN32
#define X11_SKIP_DUMMY
typedef struct _Display Display;
typedef unsigned long Window;
typedef unsigned long Atom;
typedef unsigned long XID;
#define XOpenDisplay(name) ((Display*)0)
#define XCloseDisplay(display)
#define XInternAtom(display, name, only_if_exists) 0
#define XChangeProperty(display, w, property, type, format, mode, data, nelements)
#define XFlush(display)
#define XSync(display, discard)
#define XInitThreads()
#define DefaultScreen(display) 0
#define RootWindow(display, screen) 0
#endif

#ifndef _WIN32
#include <X11/Xlib.h>
#endif
#include "fxtk.h"
#include <SDL2/SDL.h>
#include <stdio.h>


extern fx_driver_t fx_sdl_driver;
extern void sdl_update_screen(void);
extern void sdl_handle_events(void);
extern void fxtk_font_init(const char *font_path, int size);
extern int sdl_get_width(void);   // 【新增】声明
extern int sdl_get_height(void);  // 【新增】声明
extern void app_init(void);

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    XInitThreads();  /* Xlib 多线程安全, EGL 子线程保险 */
    printf("[I] SYS: Starting fxtk PC Simulator Engine (Resizable)...\n");

    /* 字体路径由 fxtk_font_init 内部 fallback 列表决定 (含 FXTK_FONT 环境变量) */
    fxtk_font_init(NULL, 18);

    fx_sdl_driver.init();
    
    // 【新增】更新驱动尺寸为实际窗口大小
    fx_sdl_driver.width = sdl_get_width();
    fx_sdl_driver.height = sdl_get_height();
    
    fx_init(&fx_sdl_driver);
    { extern void sdl_first_target(void); sdl_first_target(); }
    fx_set_bg(FX_WINDOW_BG);
    
    app_init();
    
    printf("[I] SYS: UI Ready. Entering main loop (window is resizable)...\n");
    
    while (1) {
        sdl_handle_events();  // 会处理 resize 事件
        fx_poll();
        sdl_update_screen();
        /* 帧同步: vsync 下 present 已限 60fps, 不额外 sleep 避免双重节拍抖动;
           FXTK_BENCH(无vsync) 时自旋限帧 */
        if (getenv("FXTK_BENCH")) SDL_Delay(16);
    }
    
    return 0;
}