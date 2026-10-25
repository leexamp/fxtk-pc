/**
 * fxtk_lua.h — fxtk Lua 绑定入口
 *
 * 宿主（main_lua.c / lua_host_sdl.c）只需要：
 *     luaL_requiref(L, "fxtk.core", luaopen_fxtk_core, 1);
 *     lua_pop(L, 1);
 * 然后 dofile("lua/fxtk.lua") 或 require("fxtk") 即可。
 */
#ifndef FXTK_LUA_H
#define FXTK_LUA_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C 模块名 "fxtk.core"；lua/fxtk.lua 在其上提供属性构造器与 C 风格全局安装。 */
int luaopen_fxtk_core(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* FXTK_LUA_H */
