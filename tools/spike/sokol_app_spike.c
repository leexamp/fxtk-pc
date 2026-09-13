/* spike: sokol_app + sokol_gfx 窗口路径 (Linux / Windows 双平台编译验证)
 * 目的: 确认 v2.4 的窗口后端在两个平台都能编译链接 —— 这是整个后端迁移的最大不确定项。
 * Linux:  gcc -O2 -Ithird_party/sokol spike.c -o spike -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
 * mingw:  x86_64-w64-mingw32-gcc -O2 -Ithird_party/sokol spike.c -o spike.exe \
 *           -ld3d11 -ldxgi -lole32 -luuid -lgdi32 -luser32 -lshell32 -static-libgcc
 * 说明: Windows 默认走 D3D11 (sokol_app 默认); 若改用 WGL 桌面 GL 则定义 SOKOL_GLCORE。
 */
#if defined(_WIN32)
  #define SOKOL_D3D11
#else
  #define SOKOL_GLCORE
#endif

#define SOKOL_APP_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include <stdio.h>

static struct {
    sg_pass_action pass_action;
} state;

static void init_cb(void)
{
    sg_desc desc = {0};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    state.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    state.pass_action.colors[0].clear_value = (sg_color){ 0.12f, 0.14f, 0.18f, 1.0f };
    printf("[spike] sokol_app 初始化完成, 后端=%d\n", (int)sg_query_backend());
}

static void frame_cb(void)
{
    static int n = 0;
    if (++n == 60) sapp_request_quit();   /* spike: 1 秒后自动退出, 便于脚本化冒烟 */
    sg_begin_pass(&(sg_pass){ .action = state.pass_action, .swapchain = sglue_swapchain() });
    sg_end_pass();
    sg_commit();
}

static void cleanup_cb(void) { sg_shutdown(); }

static void event_cb(const sapp_event *e)
{
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
        .width = 480,
        .height = 272,
        .window_title = "fxtk v2.4 sokol spike",
        .logger.func = slog_func,
    };
}
