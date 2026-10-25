/**
 * main_lua.c — fxtk Lua 演示宿主（sokol 后端）
 *
 * 职责边界：
 *   驱动/字体/窗口 = C 宿主；界面与交互 = Lua 脚本。
 *   app.lua 定义 function app_init()，可选 function app_frame(dt, t)。
 *
 * 用法:
 *   ./fxtk_lua                       # 默认找 demo-lua/app.lua
 *   FXTK_SCRIPT=path.lua ./fxtk_lua
 *
 * 测试钩子（与 main_sokol.c 对齐）:
 *   FXTK_SHOT / FXTK_SHOT_AT / FXTK_QUIT_AFTER / FXTK_CLICK / FXTK_CLICKS / FXTK_RESIZE / FXTK_WIN
 *   FXTK_DRAG="x0,y0,x1,y1,steps,frame"  (拖拽注入，用于画板/四边形/拖拽页自动化)
 */
#define _POSIX_C_SOURCE 200809L
#include "fxtk_sokol.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "fxtk.h"
#include "fxtk_backends.h"
#include "fxtk_lua.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void fxtk_font_init(const char *font_path, int size);
extern void fxtk_sokol_request_shot(void);
extern void fxtk_sokol_inject_click(int x, int y);
extern void fxtk_sokol_inject_drag(int x0, int y0, int x1, int y1, int steps);

static lua_State *s_L = NULL;
static char s_script[1024];
static char s_argv0[1024];
static char s_pkgpath[4096];

static int s_shot_at = 30;
static char s_shot_path[512];
static char s_shot2_path[512];
static int s_shot2_at = 0;
static int s_quit_after = 0;
static int s_win_w = 0, s_win_h = 0;
static int s_frames = 0;
static int s_lua_ok = 0;
static int s_has_frame = 0;

static void path_dir(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (!slash) { snprintf(out, cap, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n >= cap) n = cap - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void resolve_script(void)
{
    const char *env = getenv("FXTK_SCRIPT");
    if (env && env[0]) { snprintf(s_script, sizeof(s_script), "%s", env); return; }

    char dir[1024];
    path_dir(s_argv0, dir, sizeof(dir));
    const char *cand[] = {
        "demo-lua/app.lua",
        "app.lua",
        "../demo-lua/app.lua",
        NULL, NULL, NULL,
        NULL
    };
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s/demo-lua/app.lua", dir); cand[3] = buf;
    static char buf2[1024];
    snprintf(buf2, sizeof(buf2), "%s/app.lua", dir); cand[4] = buf2;
    static char buf3[1024];
    snprintf(buf3, sizeof(buf3), "%s/../demo-lua/app.lua", dir); cand[5] = buf3;

    for (int i = 0; cand[i]; i++) {
        if (file_exists(cand[i])) { snprintf(s_script, sizeof(s_script), "%s", cand[i]); return; }
    }
    snprintf(s_script, sizeof(s_script), "demo-lua/app.lua");   /* 让报错信息更直观 */
}

static int lua_traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg) luaL_traceback(L, L, msg, 1);
    else lua_pushliteral(L, "(error object is not a string)");
    return 1;
}

static int pcall_tb(lua_State *L, int nargs)
{
    int base = lua_gettop(L) - nargs;   /* 函数所在位置 */
    lua_pushcfunction(L, lua_traceback);
    lua_insert(L, base);
    int rc = lua_pcall(L, nargs, 0, base);
    lua_remove(L, base);
    return rc;
}

static void report_lua_error(const char *what)
{
    const char *err = lua_tostring(s_L, -1);
    fx_log(FX_LOG_ERROR, "[lua] %s: %s", what, err ? err : "?");
    lua_pop(s_L, 1);
}

static void boot_lua(void)
{
    s_L = luaL_newstate();
    if (!s_L) { fx_log(FX_LOG_ERROR, "[lua] luaL_newstate 失败"); return; }
    luaL_openlibs(s_L);

    /* 注册 C 绑定模块 fxtk.core；lua/fxtk.lua 会 require 它。 */
    luaL_requiref(s_L, "fxtk.core", luaopen_fxtk_core, 1);
    lua_pop(s_L, 1);

    /* package.path：脚本目录 / 仓库 lua/ 目录 / 可执行文件目录附近都找一遍。 */
    char sdir[1024], edir[1024], edir2[1024];
    path_dir(s_script, sdir, sizeof(sdir));
    path_dir(s_argv0, edir, sizeof(edir));
    snprintf(edir2, sizeof(edir2), "%s/..", edir);
    /* 用 C API 拼 package.path，而不是 luaL_dostring —— Windows 路径里的反斜杠
     * 在 Lua 字符串里是转义序列（'Z:\media...' 会报 invalid escape sequence）。 */
    snprintf(s_pkgpath, sizeof(s_pkgpath),
             "%s/?.lua;%s/../lua/?.lua;%s/?.lua;%s/lua/?.lua;"
             "%s/?.lua;%s/lua/?.lua;%s/../lua/?.lua;./lua/?.lua;./?.lua",
             sdir, sdir, edir, edir, edir2, edir2, edir2);
    lua_getglobal(s_L, "package");
    if (lua_istable(s_L, -1)) {
        lua_getfield(s_L, -1, "path");
        const char *old = lua_tostring(s_L, -1);
        char full[8192];
        snprintf(full, sizeof(full), "%s;%s", s_pkgpath, old ? old : "");
        lua_pop(s_L, 1);
        lua_pushstring(s_L, full);
        lua_setfield(s_L, -2, "path");
    }
    lua_pop(s_L, 1);

    if (luaL_loadfile(s_L, s_script) != LUA_OK) { report_lua_error(s_script); return; }
    if (pcall_tb(s_L, 0) != LUA_OK) { report_lua_error("执行脚本"); return; }

    lua_getglobal(s_L, "app_init");
    if (lua_isfunction(s_L, -1)) {
        if (pcall_tb(s_L, 0) != LUA_OK) { report_lua_error("app_init()"); return; }
    } else {
        lua_pop(s_L, 1);
        fx_log(FX_LOG_ERROR, "[lua] 脚本没有定义 function app_init()");
        return;
    }

    lua_getglobal(s_L, "app_frame");
    s_has_frame = lua_isfunction(s_L, -1);
    lua_pop(s_L, 1);
    s_lua_ok = 1;
}

static void call_lua_frame(double dt)
{
    if (!s_lua_ok) return;
    lua_getglobal(s_L, "app_frame");
    if (!lua_isfunction(s_L, -1)) { lua_pop(s_L, 1); return; }
    lua_pushnumber(s_L, dt);
    lua_pushinteger(s_L, s_frames);
    if (pcall_tb(s_L, 2) != LUA_OK) report_lua_error("app_frame()");
}

static void init_cb(void)
{
    fxtk_font_init(NULL, 18);
    fx_sokol_driver.width = (uint32_t)sapp_width();
    fx_sokol_driver.height = (uint32_t)sapp_height();
    fx_sokol_driver.init();
    fx_init(&fx_sokol_driver);
    fx_backend_set_screen(sapp_width(), sapp_height(), (float)sapp_dpi_scale());
    fx_set_bg(FX_WINDOW_BG);
    if (getenv("FXTK_NOANIM")) fx_animation(0);

    boot_lua();
    if (!s_lua_ok) { sapp_request_quit(); return; }

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
}

static void frame_cb(void)
{
    double dt = sapp_frame_duration();
    call_lua_frame(dt);

    { const char *cs = getenv("FXTK_CLICKS");
      if (cs) {
          const char *q = cs;
          while (*q) {
              int cx = 0, cy = 0, cf = 0;
              if (sscanf(q, "%d,%d,%d", &cx, &cy, &cf) == 3 && s_frames + 1 == cf)
                  fxtk_sokol_inject_click(cx, cy);
              const char *sc = strchr(q, ';');
              if (!sc) break;
              q = sc + 1;
          }
      } }
    { const char *clk = getenv("FXTK_CLICK");
      if (clk && s_frames + 1 == 20) {
          int cx = 0, cy = 0;
          if (sscanf(clk, "%d,%d", &cx, &cy) == 2) fxtk_sokol_inject_click(cx, cy);
      } }
    { const char *dg = getenv("FXTK_DRAG");
      if (dg) {
          int x0 = 0, y0 = 0, x1 = 0, y1 = 0, steps = 8, df = 20;
          if (sscanf(dg, "%d,%d,%d,%d,%d,%d", &x0, &y0, &x1, &y1, &steps, &df) >= 4 && s_frames + 1 == df)
              fxtk_sokol_inject_drag(x0, y0, x1, y1, steps > 0 ? steps : 8);
      } }
    { const char *rs = getenv("FXTK_RESIZE");
      if (rs && s_frames + 1 == 20) {
          int rw = 0, rh = 0;
          if (sscanf(rs, "%dx%d", &rw, &rh) == 2) {
              extern void fxtk_sokol_apply_size(int w, int h);
              fxtk_sokol_apply_size(rw, rh);
          }
      } }

    int want_shot  = (s_shot_path[0]  && s_frames + 1 == s_shot_at);
    int want_shot2 = (s_shot2_path[0] && s_frames + 1 == s_shot2_at);
    if (want_shot || want_shot2) fxtk_sokol_request_shot();

    fx_poll();
    fxtk_sokol_frame(sapp_width(), sapp_height());
    s_frames++;

    if (want_shot) {
        if (fx_screenshot(s_shot_path)) fx_log(FX_LOG_INFO, "[shot] %s", s_shot_path);
        else fx_log(FX_LOG_WARN, "[shot] 失败");
        if (!s_quit_after && !s_shot2_path[0]) s_quit_after = s_frames + 1;
    }
    if (want_shot2) {
        if (fx_screenshot(s_shot2_path)) fx_log(FX_LOG_INFO, "[shot2] %s", s_shot2_path);
        else fx_log(FX_LOG_WARN, "[shot2] 失败");
        s_quit_after = s_frames + 1;
    }
    if (s_quit_after > 0 && s_frames >= s_quit_after) sapp_request_quit();
}

static void cleanup_cb(void)
{
    if (s_L) { lua_close(s_L); s_L = NULL; }
}

static void event_cb(const sapp_event *e)
{
    extern void fxtk_sokol_handle_event(const sapp_event *e);
    fxtk_sokol_handle_event(e);
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_ESCAPE)
        sapp_request_quit();
}

sapp_desc sokol_main(int argc, char *argv[])
{
    (void)argc;
    if (argv && argv[0]) snprintf(s_argv0, sizeof(s_argv0), "%s", argv[0]);
    else snprintf(s_argv0, sizeof(s_argv0), "./fxtk_lua");
    resolve_script();

    static char title[128];
    snprintf(title, sizeof(title), "fxtk v%s · Lua demo (%s)", FXTK_VERSION, s_script);
    const char *hidpi_env = getenv("FXTK_HIDPI");
    bool use_hidpi = hidpi_env ? (hidpi_env[0] != '0') : true;
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .cleanup_cb = cleanup_cb,
        .event_cb = event_cb,
        .swap_interval = getenv("FXTK_NOVSYNC") ? 0 : 1,
        .width = 1280,
        .height = 720,
        .window_title = title,
        .depth_format = SAPP_PIXELFORMAT_NONE,
        .logger.func = slog_func,
        .high_dpi = use_hidpi,
    };
}
