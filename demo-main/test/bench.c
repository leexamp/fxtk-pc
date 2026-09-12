/* test/bench.c — 渲染吞吐基准 (dummy 驱动, 无 vsync 限制)
 *
 * 用法: SDL_VIDEODRIVER=dummy ./test/bench [帧数] [页号] [每N帧报一行] [窗口宽 高]
 *   页号: 0=波形 5=输入 6=画板 7=键鼠 8=压测(自动增加控件) 9=滚动 -1=默认
 *   给宽高则模拟窗口 resize (复现大窗口下压测页可达数千控件的场景)
 * 输出控件数与 ms/frame 的时间序列 —— 用来观察"控件数增长 → 帧率下降"的曲线。
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern fx_driver_t fx_sdl_driver;
extern void sdl_handle_events(void);
extern void sdl_update_screen(void);
extern void sdl_first_target(void);
extern void fxtk_font_init(const char *font_path, int size);
extern int sdl_get_width(void);
extern int sdl_get_height(void);
extern void app_init(void);
extern SDL_Renderer *fxtk_get_sdl_renderer(void);

/* 回读当前渲染目标 → PPM/PNG (A/B 像素回归与截图画廊用; SDL 软件/dummy 驱动均可) */
static void dump_ppm(const char *path)
{
    size_t plen = strlen(path);
    if (plen > 4 && strcmp(path + plen - 4, ".png") == 0) {   /* v2.4: 直接走框架截图 (stb PNG) */
        if (fx_screenshot(path)) printf("# dumped %s (PNG via fx_screenshot)\n", path);
        else printf("# dump: fx_screenshot 失败\n");
        return;
    }
    SDL_Renderer *r = fxtk_get_sdl_renderer();
    int w = 0, h = 0;
    if (!r || SDL_GetRendererOutputSize(r, &w, &h) != 0 || w <= 0) { printf("# dump: no renderer\n"); return; }
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h * 4);
    if (!buf) return;
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, buf, w * 4) != 0) {
        printf("# dump: readpixels failed: %s\n", SDL_GetError()); free(buf); return;
    }
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            unsigned char rgb[3] = { buf[i*4+2], buf[i*4+1], buf[i*4] };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("# dumped %s (%dx%d)\n", path, w, h);
    }
    free(buf);
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 1200;
    int page   = argc > 2 ? atoi(argv[2]) : 8;
    int every  = argc > 3 ? atoi(argv[3]) : 200;
    int bw     = argc > 4 ? atoi(argv[4]) : 0;
    int bh     = argc > 5 ? atoi(argv[5]) : 0;
    if (every < 1) every = 200;

    fxtk_font_init(NULL, 18);
    fx_sdl_driver.init();
    fx_sdl_driver.width  = sdl_get_width();
    fx_sdl_driver.height = sdl_get_height();
    fx_init(&fx_sdl_driver);
    sdl_first_target();
    fx_set_bg(FX_WINDOW_BG);
    app_init();
    if (page >= 0) fx_set_value(fx_find("tab"), page);

    printf("# bench: %dx%d page=%d frames=%d\n", sdl_get_width(), sdl_get_height(), page, frames);
    printf("# %8s %8s %11s %8s\n", "frame", "widgets", "ms/frame", "fps");

    double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 t0 = SDL_GetPerformanceCounter();
    double t_poll = 0, t_present = 0;
    for (int i = 1; i <= frames; i++) {
        if (bw > 0 && bh > 0 && i == 5) {   /* 第 5 帧注入一次窗口 resize */
            SDL_Event e; memset(&e, 0, sizeof e);
            e.type = SDL_WINDOWEVENT;
            e.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
            e.window.data1 = bw; e.window.data2 = bh;
            SDL_PushEvent(&e);
            printf("# resize -> %dx%d\n", bw, bh);
        }
        Uint64 a = SDL_GetPerformanceCounter();
        sdl_handle_events();
        fx_poll();
        Uint64 b = SDL_GetPerformanceCounter();
        sdl_update_screen();
        Uint64 c = SDL_GetPerformanceCounter();
        t_poll += (double)(b - a); t_present += (double)(c - b);
        if (i % every == 0) {
            Uint64 t1 = SDL_GetPerformanceCounter();
            double ms = (double)(t1 - t0) / freq * 1000.0 / every;
            printf("  %8d %8d %11.3f %8.1f   poll %.2fms  present %.2fms\n",
                   i, fxtk_widget_count(), ms, ms > 0 ? 1000.0 / ms : 0.0,
                   t_poll / freq * 1000.0 / every, t_present / freq * 1000.0 / every);
            t0 = t1; t_poll = 0; t_present = 0;
        }
    }
    if (argc > 6) dump_ppm(argv[6]);
    return 0;
}
