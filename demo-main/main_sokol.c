/**
 * main_sokol.c — sokol_app 入口 (v2.4 PC 后端)
 *
 * sokol_app 拥有主循环, 因此这里把框架的轮询契约反过来驱动:
 *   init_cb    → 字体初始化 + 驱动初始化 + fx_init + app_init
 *   frame_cb   → fx_poll() (框架逐帧逻辑) + fxtk_sokol_frame() (提交并呈现)
 *   cleanup_cb → 收尾
 *
 * 测试钩子 (与 SDL 版等价, 便于 CI 无头验证):
 *   FXTK_SHOT=<path.png>  截一帧存 PNG
 *   FXTK_SHOT_AT=<帧号>   第几帧截图 (默认 30)
 *   FXTK_QUIT_AFTER=<帧>  跑够帧数自动退出 (截图时自动退出)
 */
#define _POSIX_C_SOURCE 200809L
#include "fxtk_sokol.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "fxtk_backends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void fxtk_font_init(const char *font_path, int size);
extern void app_init(void);

static char s_shot_path[512];
static int  s_shot_at = 30;
static int  s_quit_after = 0;
static int  s_frames = 0;

static void init_cb(void)
{
    fxtk_font_init(NULL, 18);
    fx_sokol_driver.width = (uint32_t)sapp_width();
    fx_sokol_driver.height = (uint32_t)sapp_height();
    fx_sokol_driver.init();
    fx_init(&fx_sokol_driver);
    fx_backend_set_screen(sapp_width(), sapp_height(),
                          (float)sapp_dpi_scale());
    fx_set_bg(FX_WINDOW_BG);
    app_init();

    const char *shot = getenv("FXTK_SHOT");
    if (shot && shot[0]) {
        snprintf(s_shot_path, sizeof(s_shot_path), "%s", shot);
        const char *at = getenv("FXTK_SHOT_AT");
        if (at) s_shot_at = atoi(at);
        if (s_shot_at < 1) s_shot_at = 1;
    }
    const char *qa = getenv("FXTK_QUIT_AFTER");
    if (qa) s_quit_after = atoi(qa);
    fx_log(FX_LOG_INFO, "[sokol] 窗口 %dx%d dpi=%.2f", sapp_width(), sapp_height(), sapp_dpi_scale());
}

extern void fxtk_sokol_request_shot(void);

static void frame_cb(void)
{
    int want_shot = (s_shot_path[0] && s_frames + 1 == s_shot_at);
    if (want_shot) fxtk_sokol_request_shot();   /* 驱动在本帧 pass 内抓取 */
    fx_poll();
    fxtk_sokol_frame(sapp_width(), sapp_height());
    s_frames++;

    if (want_shot) {
        if (fx_screenshot(s_shot_path)) fx_log(FX_LOG_INFO, "[shot] %s", s_shot_path);
        else fx_log(FX_LOG_WARN, "[shot] 失败 (后端不支持 read_pixels?)");
        if (!s_quit_after) s_quit_after = s_frames + 1;
    }
    if (s_quit_after > 0 && s_frames >= s_quit_after) sapp_request_quit();
}

static void cleanup_cb(void) {}

static void event_cb(const sapp_event *e)
{
    extern void fxtk_sokol_handle_event(const sapp_event *e);
    fxtk_sokol_handle_event(e);
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_ESCAPE)
        sapp_request_quit();
}

sapp_desc sokol_main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .cleanup_cb = cleanup_cb,
        .event_cb = event_cb,
        .width = 1280,
        .height = 720,
        .window_title = "fxtk v2.4 · sokol",
        .depth_format = SAPP_PIXELFORMAT_NONE,   /* 2D UI: 交换链与管线都不带深度, 避免格式校验冲突 */
        .logger.func = slog_func,
        .high_dpi = false,
    };
}
