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
#include <stdlib.h>


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
    
    /* 无人值守测试钩子 (与 main_sokol.c 对齐, 便于两个后端做同样的截图/输入验证):
     *   FXTK_CLICK="x,y"      第 20 帧注入一次点击
     *   FXTK_SHOT=<path.png>  第 FXTK_SHOT_AT(默认 30) 帧截图
     *   FXTK_QUIT_AFTER=<n>   第 n 帧退出 */
    const char *clk = getenv("FXTK_CLICK");
    const char *shot_path = getenv("FXTK_SHOT");
    int shot_at = 30, quit_after = 0, frames = 0;
    { const char *a = getenv("FXTK_SHOT_AT"); if (a) shot_at = atoi(a); }
    { const char *q = getenv("FXTK_QUIT_AFTER"); if (q) quit_after = atoi(q); }
    if (shot_path && !quit_after) quit_after = shot_at + 1;

    while (1) {
        sdl_handle_events();  // 会处理 resize 事件
        frames++;
        if (clk && frames == 20) { int cx = 0, cy = 0; if (sscanf(clk, "%d,%d", &cx, &cy) == 2) { fx_touch_press(cx, cy); fx_touch_release(cx, cy); } }
        fx_poll();
        if (shot_path && frames == shot_at) printf("[shot] %s (%d)\n", fx_screenshot(shot_path) ? "ok" : "fail", fx_screenshot(shot_path) ? 1 : 0);
        sdl_update_screen();
        if (quit_after > 0 && frames >= quit_after) break;
        /* 帧同步: vsync 下 present 已限 60fps, 不额外 sleep 避免双重节拍抖动;
           FXTK_BENCH(无vsync) 时自旋限帧 */
        if (getenv("FXTK_BENCH")) SDL_Delay(16);
    }
    
    return 0;
}