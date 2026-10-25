/**
 * lua_host_sdl.c — 无头 Lua 宿主（SDL dummy 驱动）
 *
 * 用途：CI / 本地无窗口跑 Lua 脚本与绑定测试、对比截图。
 * 用法:
 *   SDL_VIDEODRIVER=dummy ./test/lua_host_sdl [script] [frames] [page] [shot.png]
 * 环境变量兜底:
 *   FXTK_SCRIPT / FXTK_FRAMES / FXTK_PAGE / FXTK_SHOT
 *
 * 与 sokol 宿主的区别只有平台层；Lua 约定完全一致：
 *   app.lua 定义 function app_init()，可选 function app_frame(dt, frame)。
 */
#define _POSIX_C_SOURCE 200809L
#include "fxtk.h"
#include "fxtk_backends.h"
#include "fxtk_lua.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

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

static lua_State *L = NULL;
static int s_fail = 0;
static int s_has_frame = 0;

static int lua_traceback(lua_State *L_)
{
    const char *msg = lua_tostring(L_, 1);
    if (msg) luaL_traceback(L_, L_, msg, 1);
    else lua_pushliteral(L_, "(error object is not a string)");
    return 1;
}

static int pcall_tb(lua_State *L_, int nargs)
{
    int base = lua_gettop(L_) - nargs;
    lua_pushcfunction(L_, lua_traceback);
    lua_insert(L_, base);
    int rc = lua_pcall(L_, nargs, 0, base);
    lua_remove(L_, base);
    return rc;
}

static void report_error(const char *what)
{
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "[lua] %s: %s\n", what, err ? err : "?");
    s_fail = 1;
    lua_pop(L, 1);
}

/* package.path 覆盖脚本目录 / 仓库 lua/ 目录 / 当前目录。 */
static void setup_package_path(const char *script)
{
    char dir[1024];
    const char *slash = strrchr(script, '/');
    if (slash) { size_t n = (size_t)(slash - script); if (n >= sizeof(dir)) n = sizeof(dir)-1; memcpy(dir, script, n); dir[n] = 0; }
    else snprintf(dir, sizeof(dir), ".");
    char code[4096];
    snprintf(code, sizeof(code),
             "package.path = '%s/?.lua;%s/../lua/?.lua;%s/../../lua/?.lua;"
             "lua/?.lua;../lua/?.lua;./?.lua;' .. package.path", dir, dir, dir);
    if (luaL_dostring(L, code) != LUA_OK) report_error("设置 package.path");
}

static void boot_lua(const char *script)
{
    L = luaL_newstate();
    if (!L) { fprintf(stderr, "[lua] luaL_newstate 失败\n"); s_fail = 1; return; }
    luaL_openlibs(L);
    luaL_requiref(L, "fxtk.core", luaopen_fxtk_core, 1);
    lua_pop(L, 1);
    setup_package_path(script);
    if (luaL_loadfile(L, script) != LUA_OK) { report_error(script); return; }
    if (pcall_tb(L, 0) != LUA_OK) { report_error("执行脚本"); return; }
    lua_getglobal(L, "app_init");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); fprintf(stderr, "[lua] 脚本没有定义 app_init()\n"); s_fail = 1; return; }
    if (pcall_tb(L, 0) != LUA_OK) { report_error("app_init()"); return; }
    lua_getglobal(L, "app_frame");
    s_has_frame = lua_isfunction(L, -1);
    lua_pop(L, 1);
}

static void call_frame(double dt, int frame)
{
    if (!L || !s_has_frame) return;
    lua_getglobal(L, "app_frame");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, dt);
    lua_pushinteger(L, frame);
    if (pcall_tb(L, 2) != LUA_OK) report_error("app_frame()");
}

static void push_resize(int w, int h)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    e.window.data1 = w;
    e.window.data2 = h;
    SDL_PushEvent(&e);
}

int main(int argc, char **argv)
{
    const char *script = (argc > 1) ? argv[1] : getenv("FXTK_SCRIPT");
    if (!script || !script[0]) script = "demo-lua/app.lua";
    int frames = (argc > 2) ? atoi(argv[2]) : (getenv("FXTK_FRAMES") ? atoi(getenv("FXTK_FRAMES")) : 60);
    int page   = (argc > 3) ? atoi(argv[3]) : (getenv("FXTK_PAGE") ? atoi(getenv("FXTK_PAGE")) : -1);
    const char *shot = (argc > 4) ? argv[4] : getenv("FXTK_SHOT");
    int win_w = getenv("FXTK_WIN_W") ? atoi(getenv("FXTK_WIN_W")) : 1280;
    int win_h = getenv("FXTK_WIN_H") ? atoi(getenv("FXTK_WIN_H")) : 720;
    if (frames < 1) frames = 1;

    fxtk_font_init(NULL, 18);
    if (fx_sdl_driver.init() != 0) return 1;
    fx_sdl_driver.width = (uint16_t)sdl_get_width();
    fx_sdl_driver.height = (uint16_t)sdl_get_height();
    fx_init(&fx_sdl_driver);
    sdl_first_target();
    fx_backend_set_screen(sdl_get_width(), sdl_get_height(), 1.0f);
    fx_set_bg(FX_WINDOW_BG);

    boot_lua(script);
    if (s_fail) return 1;

    if (page >= 0) {
        fx_widget_t *tab = fx_find("tab");
        if (tab) fx_set_value(tab, page);
    }

    push_resize(win_w, win_h);

    for (int i = 1; i <= frames; i++) {
        sdl_handle_events();
        call_frame(1.0 / 60.0, i);
        fx_poll();
        sdl_update_screen();
    }

    if (shot && shot[0]) {
        if (fx_screenshot(shot)) printf("[shot] %s (%dx%d)\n", shot, sdl_get_width(), sdl_get_height());
        else { fprintf(stderr, "[shot] 失败: %s\n", shot); s_fail = 1; }
    }
    if (L) lua_close(L);
    return s_fail ? 1 : 0;
}
