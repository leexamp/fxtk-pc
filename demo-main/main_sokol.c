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
static char s_shot2_path[512];      /* 同一次运行内的第二次截图 (脏区残留检测) */
static int  s_shot2_at = 0;
static int  s_shot_at = 30;
static int  s_win_w = 0, s_win_h = 0;   /* FXTK_WIN="WxH": 指定初始窗口尺寸(复现特定分辨率下的问题) */
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

    { const char *wn = getenv("FXTK_WIN"); if (wn) sscanf(wn, "%dx%d", &s_win_w, &s_win_h); }
    const char *shot = getenv("FXTK_SHOT");
    if (shot && shot[0]) {
        snprintf(s_shot_path, sizeof(s_shot_path), "%s", shot);
        const char *at = getenv("FXTK_SHOT_AT");
        if (at) s_shot_at = atoi(at);
        if (s_shot_at < 1) s_shot_at = 1;
    }
    const char *shot2 = getenv("FXTK_SHOT2");
    if (shot2 && shot2[0]) {
        snprintf(s_shot2_path, sizeof(s_shot2_path), "%s", shot2);
        const char *at2 = getenv("FXTK_SHOT2_AT");
        s_shot2_at = at2 ? atoi(at2) : 120;
    }
    const char *qa = getenv("FXTK_QUIT_AFTER");
    if (qa) s_quit_after = atoi(qa);
    fx_log(FX_LOG_INFO, "[sokol] 窗口 %dx%d dpi=%.2f", sapp_width(), sapp_height(), sapp_dpi_scale());
}

extern void fxtk_sokol_request_shot(void);
extern void fxtk_sokol_inject_click(int x, int y);   /* 测试: 注入一次点击 */

static void frame_cb(void)
{
    {   /* 测试钩子: FXTK_DRAG="x0,y0,x1,y1[,steps]" 在第 30 帧注入一次拖拽 */
        const char *dg = getenv("FXTK_DRAG");
        static int drag_frame = -1;
        if (drag_frame < 0) {
            drag_frame = 30;
            if (dg) { int a,b,c,d,e,f; if (sscanf(dg, "%d,%d,%d,%d,%d,%d", &a,&b,&c,&d,&e,&f) == 6) drag_frame = f; }
        }
        if (dg && s_frames + 1 == drag_frame) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0, st = 12;
            int n = sscanf(dg, "%d,%d,%d,%d,%d", &x0, &y0, &x1, &y1, &st);
            if (n >= 4) {
                extern void fxtk_sokol_inject_drag(int, int, int, int, int);
                fxtk_sokol_inject_drag(x0, y0, x1, y1, st > 0 ? st : 12);
                fx_log(FX_LOG_INFO, "[test] 注入拖拽 (%d,%d)->(%d,%d)", x0, y0, x1, y1);
            }
        }
    }
    {   /* 测试钩子: FXTK_DRAG_LOOP="x0,y0,x1,y1[,steps[,每帧事件数]]" 持续拖动, 测"跟手"延迟 */
        const char *dl = getenv("FXTK_DRAG_LOOP");
        if (dl) {
            extern void fxtk_sokol_drag_loop_start(int, int, int, int, int, int);
            extern void fxtk_sokol_drag_loop_tick(void);
            static int started = 0;
            static int dl_frame = -1;
            if (dl_frame < 0) {
                dl_frame = 20;
                { int a,b,c,d,e,f,g; if (sscanf(dl, "%d,%d,%d,%d,%d,%d,%d", &a,&b,&c,&d,&e,&f,&g) == 7) dl_frame = g; }
            }
            if (!started && s_frames + 1 >= dl_frame) {
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0, st = 24, pf = 3;
                if (sscanf(dl, "%d,%d,%d,%d,%d,%d", &x0, &y0, &x1, &y1, &st, &pf) >= 4) {
                    fxtk_sokol_drag_loop_start(x0, y0, x1, y1, st, pf);
                    started = 1;
                    fx_log(FX_LOG_INFO, "[test] 持续拖动 (%d,%d)->(%d,%d) %d步 每帧%d事件", x0, y0, x1, y1, st, pf);
                }
            } else if (started) {
                fxtk_sokol_drag_loop_tick();
            }
        }
    }
    {   /* 测试钩子: FXTK_CLICKS="x,y,帧;x,y,帧;..." 多段点击(测跨页切换残留/IPC) */
        const char *cs = getenv("FXTK_CLICKS");
        if (cs) {
            const char *q = cs;
            while (*q) {
                int cx = 0, cy = 0, cf = 0;
                if (sscanf(q, "%d,%d,%d", &cx, &cy, &cf) == 3 && s_frames + 1 == cf) {
                    fxtk_sokol_inject_click(cx, cy);
                    fx_log(FX_LOG_INFO, "[test] 点击 (%d,%d) @帧%d", cx, cy, cf);
                }
                const char *sc = strchr(q, ';');
                if (!sc) break;
                q = sc + 1;
            }
        }
    }
    {   /* 测试钩子: FXTK_RESIZE="WxH" 在第 20 帧模拟一次窗口缩放 */
        const char *rs = getenv("FXTK_RESIZE");
        if (rs && s_frames + 1 == 20) {
            int rw = 0, rh = 0;
            if (sscanf(rs, "%dx%d", &rw, &rh) == 2) {
                extern void fxtk_sokol_apply_size(int w, int h);
                fxtk_sokol_apply_size(rw, rh);
                fx_log(FX_LOG_INFO, "[test] 模拟缩放 -> %dx%d", rw, rh);
            }
        }
    }
    {   /* 测试钩子: FXTK_CLICK="x,y" 在第 20 帧注入一次点击(用于无人值守验证输入链路) */
        const char *clk = getenv("FXTK_CLICK");
        if (clk && s_frames + 1 == 20) {
            int cx = 0, cy = 0;
            if (sscanf(clk, "%d,%d", &cx, &cy) == 2) fxtk_sokol_inject_click(cx, cy);
        }
    }
    int want_shot  = (s_shot_path[0]  && s_frames + 1 == s_shot_at);
    int want_shot2 = (s_shot2_path[0] && s_frames + 1 == s_shot2_at);
    if (want_shot || want_shot2) fxtk_sokol_request_shot();   /* 驱动在本帧 pass 内抓取 */
    {   /* 整帧计时(框架逐帧 + 像素层 + 提交), FXTK_STAT 汇总打印: 光看 fps 判断不出"手感延迟" */
        extern uint32_t fxtk_sokol_now_us(void);
        extern void fxtk_sokol_note_frame(uint32_t us);
        uint32_t t0 = fxtk_sokol_now_us();
        fx_poll();
        fxtk_sokol_frame(sapp_width(), sapp_height());
        fxtk_sokol_note_frame(fxtk_sokol_now_us() - t0);
    }
    s_frames++;

    if (want_shot) {
        if (fx_screenshot(s_shot_path)) fx_log(FX_LOG_INFO, "[shot] %s", s_shot_path);
        else fx_log(FX_LOG_WARN, "[shot] 失败 (后端不支持 read_pixels?)");
        if (!s_quit_after && !s_shot2_path[0]) s_quit_after = s_frames + 1;   /* 配了第二次截图就不提前退出 */
    }
    if (want_shot2) {
        if (fx_screenshot(s_shot2_path)) fx_log(FX_LOG_INFO, "[shot2] %s", s_shot2_path);
        s_quit_after = s_frames + 1;
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
    static char s_title[64];
    snprintf(s_title, sizeof(s_title), "fxtk v%s · sokol", FXTK_VERSION);
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .cleanup_cb = cleanup_cb,
        .event_cb = event_cb,
        .swap_interval = getenv("FXTK_NOVSYNC") ? 0 : 1,   /* 0 = 关垂直同步(测峰值帧率用) */
        .width = (int)(s_win_w ? s_win_w : 1280),
        .height = (int)(s_win_h ? s_win_h : 720),
        .window_title = s_title,
        .depth_format = SAPP_PIXELFORMAT_NONE,   /* 2D UI: 交换链与管线都不带深度, 避免格式校验冲突 */
        .logger.func = slog_func,
        .high_dpi = false,
    };
}
