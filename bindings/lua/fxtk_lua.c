/**
 * fxtk_lua.c — fxtk C 核心的 Lua 绑定（Lua 5.4）
 *
 * 设计目标：让 Lua 里的写法与 C 的属性宏尽量一一对应。
 *
 *   C:  fx_button_new(pixel("16,56","200,96"), name("ok"), title("确定"), call(on_ok));
 *   Lua: fx_button_new(pixel("16,56","200,96"), name("ok"), title("确定"), call(on_ok))
 *
 * 绑定只做三件事：
 *   1. 把 C 的 fx_* 函数按原签名暴露成 Lua 函数（个别 out 参数改成多返回值）；
 *   2. 属性（pixel/title/call/...）用普通 Lua table 表示，由 lua/fxtk.lua 构造；
 *   3. 回调：C 的 fx_cb_t 统一转成 Lua 回调（list 类回调额外拿到 index）。
 *
 * 控件句柄用 lightuserdata（指向框架静态控件池，不需要 Lua 释放）；
 * 图片 / SVG 是带 __gc 的 full userdata（由 Lua 负责释放）。
 *
 * 本文件只依赖 fxtk 公共头 + Lua 头，不依赖任何后端；sokol/SDL 的初始化由宿主负责。
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "fxtk.h"
#include "fxtk_backends.h"
#include "fxtk_desktop.h"
#include "fxtk_effects.h"
#include "fxtk_image.h"

#define FXTK_LUA_VERSION "0.1.0"
#define FXTK_CBS_KEY     "fxtk.cbs"
#define FXTK_GRID_KEY    "fxtk.gridnames"

/* ==================== 基础工具 ==================== */

static lua_State *g_L = NULL;

static fx_widget_t *check_widget(lua_State *L, int idx)
{
    if (lua_isnoneornil(L, idx)) return NULL;
    if (!lua_islightuserdata(L, idx))
        luaL_argerror(L, idx, "fxtk widget expected (returned by fx_*_new/fx_find)");
    return (fx_widget_t *)lua_touserdata(L, idx);
}

static void push_widget(lua_State *L, fx_widget_t *w)
{
    if (w) lua_pushlightuserdata(L, w);
    else   lua_pushnil(L);
}

static int check_int(lua_State *L, int idx)    { return (int)luaL_checkinteger(L, idx); }
static float check_num(lua_State *L, int idx)  { return (float)luaL_checknumber(L, idx); }
static const char *check_str(lua_State *L, int idx) { return luaL_checkstring(L, idx); }
static fx_color_t check_color(lua_State *L, int idx)
{
    /* 颜色在 Lua 里是普通整数: 0xRRGGBB 或 0xAARRGGBB (与 C 的 fx_color_t 一致) */
    return (fx_color_t)(uint32_t)luaL_checkinteger(L, idx);
}

static fx_colorx_t check_colorx(lua_State *L, int idx)
{
    fx_colorx_t c;
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "light");
        lua_getfield(L, idx, "dark");
        c.light = (fx_color_t)(uint32_t)luaL_checkinteger(L, -2);
        c.dark  = (fx_color_t)(uint32_t)luaL_checkinteger(L, -1);
        lua_pop(L, 2);
    } else {
        c.light = c.dark = check_color(L, idx);
    }
    return c;
}

/* ==================== 回调：C fx_cb_t -> Lua ==================== */

static int lua_traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg) luaL_traceback(L, L, msg, 1);
    else lua_pushliteral(L, "(error object is not a string)");
    return 1;
}

/* 取注册表里的回调表；不存在则创建。返回时表在栈顶。 */
static void push_cb_table(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, FXTK_CBS_KEY);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, FXTK_CBS_KEY);
}

/* 把栈上 idx 处的函数登记为该控件的回调。 */
static void register_widget_cb(lua_State *L, fx_widget_t *w, int fnidx)
{
    if (!w) return;
    fnidx = (fnidx < 0) ? lua_gettop(L) + fnidx + 1 : fnidx;
    push_cb_table(L);
    lua_pushlightuserdata(L, w);
    lua_pushvalue(L, fnidx);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    /* C 侧回调由调用方设置：控件构造 -> fx_set_cb(w, lua_widget_cb)，
     * list 回调 -> fx_list_set_cb(w, lua_list_cb)。这里只维护 Lua 函数表。 */
}

static void clear_widget_cb(lua_State *L, fx_widget_t *w)
{
    if (!w) return;
    push_cb_table(L);
    lua_pushlightuserdata(L, w);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

/* 通用派发：先取函数，再压 (w[, ud_int]) 并 pcall。 */
static void lua_dispatch(fx_widget_t *w, void *ud, int pass_ud)
{
    lua_State *L = g_L;
    if (!L || !w) return;
    int top = lua_gettop(L);

    push_cb_table(L);
    lua_pushlightuserdata(L, w);
    lua_rawget(L, -2);
    if (!lua_isfunction(L, -1)) { lua_settop(L, top); return; }

    int fnidx = lua_gettop(L);
    lua_pushcfunction(L, lua_traceback);
    int erridx = lua_gettop(L);
    lua_pushvalue(L, fnidx);                 /* function */
    lua_pushlightuserdata(L, w);             /* arg 1 */
    int nargs = 1;
    if (pass_ud) { lua_pushinteger(L, (lua_Integer)(intptr_t)ud); nargs = 2; }

    if (lua_pcall(L, nargs, 0, erridx) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fx_log(FX_LOG_ERROR, "[lua] 控件回调出错: %s", err ? err : "?");
    }
    lua_settop(L, top);
}

static void lua_widget_cb(fx_widget_t *w, void *ud) { (void)ud; lua_dispatch(w, NULL, 0); }
static void lua_list_cb(fx_widget_t *w, void *ud)   { lua_dispatch(w, ud, 1); }

/* ==================== 图片 / SVG userdata ==================== */

typedef struct { fx_image_t *img; } lua_image_t;
typedef struct { fx_svg_t   *svg; } lua_svg_t;

#define FXTK_IMAGE_MT "fxtk.Image"
#define FXTK_SVG_MT   "fxtk.Svg"

static lua_image_t *push_image(lua_State *L, fx_image_t *img)
{
    if (!img) { lua_pushnil(L); return NULL; }
    lua_image_t *im = (lua_image_t *)lua_newuserdatauv(L, sizeof(lua_image_t), 0);
    im->img = img;
    luaL_setmetatable(L, FXTK_IMAGE_MT);
    return im;
}

static fx_image_t *check_image(lua_State *L, int idx)
{
    lua_image_t *im = (lua_image_t *)luaL_checkudata(L, idx, FXTK_IMAGE_MT);
    if (!im->img) luaL_error(L, "image has been freed");
    return im->img;
}

static int l_image_gc(lua_State *L)
{
    lua_image_t *im = (lua_image_t *)luaL_checkudata(L, 1, FXTK_IMAGE_MT);
    if (im->img) { fx_image_free(im->img); im->img = NULL; }
    return 0;
}

static int l_image_tostring(lua_State *L)
{
    lua_image_t *im = (lua_image_t *)luaL_checkudata(L, 1, FXTK_IMAGE_MT);
    if (!im->img) lua_pushliteral(L, "fxtk.Image(freed)");
    else lua_pushfstring(L, "fxtk.Image(%dx%d)", (int)im->img->w, (int)im->img->h);
    return 1;
}

/* 把图片 userdata 绑到控件上（防止图片被 Lua GC 掉、控件还握着裸指针）。
 * 只记强引用，不做所有权转移；fx.delete 时删除引用。 */
static void attach_image(lua_State *L, fx_widget_t *w, int imgidx)
{
    if (!w) return;
    lua_getfield(L, LUA_REGISTRYINDEX, "fxtk.imgrefs");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "fxtk.imgrefs");
    }
    lua_pushlightuserdata(L, w);
    lua_pushvalue(L, imgidx);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void detach_image(lua_State *L, fx_widget_t *w)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "fxtk.imgrefs");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_pushlightuserdata(L, w);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static lua_svg_t *push_svg(lua_State *L, fx_svg_t *svg)
{
    if (!svg) { lua_pushnil(L); return NULL; }
    lua_svg_t *s = (lua_svg_t *)lua_newuserdatauv(L, sizeof(lua_svg_t), 0);
    s->svg = svg;
    luaL_setmetatable(L, FXTK_SVG_MT);
    return s;
}

static fx_svg_t *check_svg(lua_State *L, int idx)
{
    lua_svg_t *s = (lua_svg_t *)luaL_checkudata(L, idx, FXTK_SVG_MT);
    if (!s->svg) luaL_error(L, "svg has been freed");
    return s->svg;
}

static int l_svg_gc(lua_State *L)
{
    lua_svg_t *s = (lua_svg_t *)luaL_checkudata(L, 1, FXTK_SVG_MT);
    if (s->svg) { fx_svg_free(s->svg); s->svg = NULL; }
    return 0;
}

/* ==================== 属性表 -> fx_attr_t[] ==================== */

/* 读取属性表里的字符串字段；字符串被属性表强引用，函数返回前有效。 */
static const char *attr_str(lua_State *L, int t, const char *field)
{
    lua_getfield(L, t, field);
    const char *s = luaL_checkstring(L, -1);
    lua_pop(L, 1);
    return s;
}

static int attr_int(lua_State *L, int t, const char *field)
{
    lua_getfield(L, t, field);
    int v = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return v;
}

static int attr_tag_of(lua_State *L, int idx, const char **tag)
{
    *tag = NULL;
    if (!lua_istable(L, idx)) return 0;
    lua_getfield(L, idx, "__fx");
    const char *t = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (!t) return 0;
    *tag = t;
    return 1;
}

static void pin_string(lua_State *L, int idx)
{
    lua_getfield(L, LUA_REGISTRYINDEX, FXTK_GRID_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, FXTK_GRID_KEY);
    }
    lua_pushvalue(L, idx);
    lua_pushboolean(L, 1);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

/* 从 first 开始的参数全是属性表；返回附加在末尾的 FX_A_NONE 项。
 * call/image 属性需要建完控件后再处理，这里把它们的参数下标记进 call_idx/img_idx。 */
static fx_widget_t *create_widget_from(lua_State *L, int type, int first)
{
    enum { MAX_ATTRS = 64 };
    fx_attr_t attrs[MAX_ATTRS + 1];
    int call_idx[MAX_ATTRS], ncall = 0;
    int img_idx[MAX_ATTRS],  nimg  = 0;
    int top = lua_gettop(L);
    int ai = 0;

    for (int i = first; i <= top; i++) {
        if (lua_isnoneornil(L, i)) continue;
        const char *tag = NULL;
        if (!attr_tag_of(L, i, &tag))
            luaL_argerror(L, i, "attribute expected: pixel()/percent()/grid()/title()/name()/call()/...");

        if (ai >= MAX_ATTRS) luaL_error(L, "too many attributes (max %d)", MAX_ATTRS);
        memset(&attrs[ai], 0, sizeof(fx_attr_t));

        if (!strcmp(tag, "pixel")) {
            attrs[ai] = pixel(attr_str(L, i, "a"), attr_str(L, i, "b"));
        } else if (!strcmp(tag, "percent")) {
            attrs[ai] = percent(attr_str(L, i, "a"), attr_str(L, i, "b"));
        } else if (!strcmp(tag, "grid")) {
            const char *name = attr_str(L, i, "name");
            int n = attr_int(L, i, "n");
            if (n == 4) {
                attrs[ai] = grid5(name, attr_int(L, i, "r1"), attr_int(L, i, "c1"),
                                        attr_int(L, i, "r2"), attr_int(L, i, "c2"));
            } else {
                attrs[ai] = grid1(name);
            }
            lua_getfield(L, i, "name");
            pin_string(L, lua_gettop(L));   /* grid 名要求静态生命周期 -> 由注册表钉住 */
            lua_pop(L, 1);
        } else if (!strcmp(tag, "title") || !strcmp(tag, "text")) {
            attrs[ai] = title(attr_str(L, i, "s"));
        } else if (!strcmp(tag, "name")) {
            attrs[ai] = name(attr_str(L, i, "s"));
        } else if (!strcmp(tag, "call")) {
            attrs[ai].tag = FX_A_CALL;
            call_idx[ncall++] = i;
        } else if (!strcmp(tag, "line")) {
            attrs[ai] = line(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "row")) {
            attrs[ai] = row(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "color")) {
            attrs[ai] = color((fx_color_t)(uint32_t)attr_int(L, i, "c"));
        } else if (!strcmp(tag, "fgcolor")) {
            attrs[ai] = fgcolor((fx_color_t)(uint32_t)attr_int(L, i, "c"));
        } else if (!strcmp(tag, "dense")) {
            attrs[ai] = dense();
        } else if (!strcmp(tag, "border")) {
            attrs[ai] = border(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "radius")) {
            attrs[ai] = radius(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "value")) {
            attrs[ai] = value(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "page")) {
            attrs[ai] = page(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "anim")) {
            attrs[ai] = anim(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "sidebar")) {
            attrs[ai] = sidebar(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "maxlen")) {
            attrs[ai] = maxlen(attr_int(L, i, "n"));
        } else if (!strcmp(tag, "image")) {
            attrs[ai].tag = FX_A_IMAGE;
            img_idx[nimg++] = i;
        } else if (!strcmp(tag, "widget")) {
            lua_getfield(L, i, "w");
            attrs[ai] = fx_wptr(check_widget(L, lua_gettop(L)));
            lua_pop(L, 1);
        } else {
            luaL_error(L, "unknown attribute '%s'", tag);
        }
        ai++;
    }
    memset(&attrs[ai], 0, sizeof(fx_attr_t));
    attrs[ai].tag = FX_A_NONE;

    fx_widget_t *w = fx_widget_new_impl(type, attrs);
    if (!w) { lua_pushnil(L); return NULL; }

    /* image 属性：先取 userdata，挂到控件上（防 GC） */
    for (int k = 0; k < nimg; k++) {
        lua_getfield(L, img_idx[k], "img");
        if (luaL_testudata(L, -1, FXTK_IMAGE_MT)) {
            fx_image_t *im = check_image(L, lua_gettop(L));
            /* 直接写控件字段（公共 API 没有“带 image 属性创建”的路径） */
            extern void fx_set_image(fx_widget_t *, fx_image_t *);
            fx_set_image(w, im);
            attach_image(L, w, lua_gettop(L));
        }
        lua_pop(L, 1);
    }
    /* call 属性：控件已建好，再登记 Lua 回调 */
    for (int k = 0; k < ncall; k++) {
        lua_getfield(L, call_idx[k], "fn");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); luaL_error(L, "call() expects a Lua function"); }
        register_widget_cb(L, w, lua_gettop(L));
        lua_pop(L, 1);
        fx_set_cb(w, lua_widget_cb, NULL);
    }

    push_widget(L, w);
    return w;
}

/* 各类控件构造器 */
#define DEF_NEW_WIDGET(fname, type) \
    static int fname(lua_State *L) { create_widget_from(L, (type), 1); return 1; }
DEF_NEW_WIDGET(l_fx_button_new,   FX_W_BUTTON)
DEF_NEW_WIDGET(l_fx_label_new,    FX_W_LABEL)
DEF_NEW_WIDGET(l_fx_grid_map,     FX_W_GRID)
DEF_NEW_WIDGET(l_fx_canvas_new,   FX_W_CANVAS)
DEF_NEW_WIDGET(l_fx_slider_new,   FX_W_SLIDER)
DEF_NEW_WIDGET(l_fx_progress_new, FX_W_PROGRESS)
DEF_NEW_WIDGET(l_fx_checkbox_new, FX_W_CHECKBOX)
DEF_NEW_WIDGET(l_fx_panel_new,    FX_W_PANEL)
DEF_NEW_WIDGET(l_fx_tab_new,      FX_W_TAB)
DEF_NEW_WIDGET(l_fx_textedit_new, FX_W_TEXTEDIT)
DEF_NEW_WIDGET(l_fx_scroll_new,   FX_W_SCROLL)
DEF_NEW_WIDGET(l_fx_image_new,    FX_W_IMAGE)
#undef DEF_NEW_WIDGET

static int l_fx_widget_new(lua_State *L)
{
    int type = check_int(L, 1);
    create_widget_from(L, type, 2);
    return 1;
}

/* ==================== 控件操作 ==================== */

static int l_fx_find(lua_State *L)
{
    push_widget(L, fx_find(check_str(L, 1)));
    return 1;
}

static int l_fx_parent(lua_State *L)
{
    fx_parent(check_widget(L, 1));
    return 0;
}

static int l_fx_delete(lua_State *L)
{
    enum { MAX_DEL = 64 };
    fx_attr_t attrs[MAX_DEL + 1];
    int n = lua_gettop(L);
    if (n > MAX_DEL) n = MAX_DEL;
    int ai = 0;
    for (int i = 1; i <= n; i++) {
        fx_widget_t *w = NULL;
        if (lua_isstring(L, i)) {
            w = fx_find(lua_tostring(L, i));
        } else if (lua_islightuserdata(L, i)) {
            w = check_widget(L, i);
        } else {
            const char *tag = NULL;
            if (attr_tag_of(L, i, &tag) && !strcmp(tag, "grid")) {
                lua_getfield(L, i, "name");
                w = fx_find(lua_tostring(L, -1));
                lua_pop(L, 1);
            } else if (attr_tag_of(L, i, &tag) && !strcmp(tag, "widget")) {
                lua_getfield(L, i, "w");
                w = check_widget(L, lua_gettop(L));
                lua_pop(L, 1);
            }
        }
        if (w) {
            clear_widget_cb(L, w);
            detach_image(L, w);
            attrs[ai++] = fx_wptr(w);
        }
    }
    attrs[ai].tag = FX_A_NONE;
    memset(&attrs[ai].v, 0, sizeof(attrs[ai].v));
    fx_delete_impl(attrs);
    return 0;
}

static int l_fx_set_title(lua_State *L)
{
    fx_set_title(check_widget(L, 1), check_str(L, 2));
    return 0;
}
static int l_fx_set_color_w(lua_State *L)   { fx_set_color_w(check_widget(L, 1), check_color(L, 2)); return 0; }
static int l_fx_set_fgcolor_w(lua_State *L) { fx_set_fgcolor_w(check_widget(L, 1), check_color(L, 2)); return 0; }
static int l_fx_set_value(lua_State *L)     { fx_set_value(check_widget(L, 1), check_int(L, 2)); return 0; }
static int l_fx_get_value(lua_State *L)     { lua_pushinteger(L, fx_get_value(check_widget(L, 1))); return 1; }
static int l_fx_set_visible(lua_State *L)   { fx_set_visible(check_widget(L, 1), check_int(L, 2)); return 0; }
static int l_fx_widget_type(lua_State *L)   { lua_pushinteger(L, fx_widget_type(check_widget(L, 1))); return 1; }

static int l_fx_widget_title(lua_State *L)
{
    const char *s = fx_widget_title(check_widget(L, 1));
    if (s) lua_pushstring(L, s); else lua_pushnil(L);
    return 1;
}

static int l_fx_set_cb(lua_State *L)
{
    fx_widget_t *w = check_widget(L, 1);
    if (lua_isnoneornil(L, 2)) { clear_widget_cb(L, w); fx_set_cb(w, NULL, NULL); return 0; }
    luaL_checktype(L, 2, LUA_TFUNCTION);
    register_widget_cb(L, w, 2);
    fx_set_cb(w, lua_widget_cb, NULL);
    return 0;
}

static int l_fx_widget_set_rect(lua_State *L)
{
    fx_widget_set_rect(check_widget(L, 1), check_int(L, 2), check_int(L, 3), check_int(L, 4), check_int(L, 5));
    return 0;
}

static int l_fx_widget_rect(lua_State *L)
{
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    fx_widget_rect(check_widget(L, 1), &x1, &y1, &x2, &y2);
    lua_pushinteger(L, x1); lua_pushinteger(L, y1);
    lua_pushinteger(L, x2); lua_pushinteger(L, y2);
    return 4;
}

static int l_fx_layout(lua_State *L) { (void)L; fx_layout(); return 0; }
static int l_fx_repaint(lua_State *L) { (void)L; fx_repaint(); return 0; }
static int l_fx_repaint_rect(lua_State *L) { fx_repaint_rect(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_set_max_scale(lua_State *L) { fx_set_max_scale(check_num(L, 1)); return 0; }
static int l_fx_set_fit(lua_State *L) { fx_set_fit(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fxtk_apply_fit(lua_State *L) { fxtk_apply_fit(check_widget(L,1)); return 0; }
static int l_fxtk_unlink(lua_State *L) { fxtk_unlink(check_widget(L,1)); return 0; }
static int l_fxtk_raise(lua_State *L) { fxtk_raise(check_widget(L,1)); return 0; }
static int l_fxtk_lower(lua_State *L) { fxtk_lower(check_widget(L,1)); return 0; }

static int l_fx_root_new(lua_State *L) { push_widget(L, fx_root_new()); return 1; }
static int l_fx_root_select(lua_State *L) { fx_root_select(check_widget(L,1)); return 0; }
static int l_fx_root_active(lua_State *L) { push_widget(L, fx_root_active()); return 1; }

static int l_fx_set_focus(lua_State *L) { fx_set_focus(check_widget(L,1)); return 0; }
static int l_fx_get_focus(lua_State *L) { push_widget(L, fx_get_focus()); return 1; }
static int l_fx_pressed(lua_State *L) { push_widget(L, fx_pressed()); return 1; }

static int l_fx_touch_state(lua_State *L)
{
    int x = 0, y = 0, p = 0;
    fx_touch_state(&x, &y, &p);
    lua_pushinteger(L, x); lua_pushinteger(L, y); lua_pushinteger(L, p);
    return 3;
}

static int l_fx_set_fontsize(lua_State *L) { fx_set_fontsize(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fx_set_align(lua_State *L)    { fx_set_align(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fx_textedit_set_readonly(lua_State *L) { fx_textedit_set_readonly(check_widget(L,1), check_int(L,2)); return 0; }

static int l_fx_textedit_text(lua_State *L)
{
    const char *s = fx_textedit_text(check_widget(L, 1));
    if (s) lua_pushstring(L, s); else lua_pushnil(L);
    return 1;
}

static int l_fx_set_image(lua_State *L)
{
    fx_widget_t *w = check_widget(L, 1);
    if (lua_isnoneornil(L, 2)) { fx_set_image(w, NULL); detach_image(L, w); return 0; }
    fx_set_image(w, check_image(L, 2));
    attach_image(L, w, 2);
    return 0;
}
static int l_fx_image_set_zoom(lua_State *L) { fx_image_set_zoom(check_widget(L,1), check_int(L,2)); return 0; }

/* ---- list / drop ---- */
static int l_fx_list_new(lua_State *L)
{
    push_widget(L, fx_list_new(check_str(L,1), check_str(L,2)));
    return 1;
}
static int l_fx_list_new_p(lua_State *L)
{
    push_widget(L, fx_list_new_p(check_str(L,1), check_str(L,2), check_int(L,3)));
    return 1;
}
static int l_fx_list_add(lua_State *L) { fx_list_add(check_widget(L,1), check_str(L,2)); return 0; }
static int l_fx_list_clear(lua_State *L) { fx_list_clear(check_widget(L,1)); return 0; }
static int l_fx_list_sel(lua_State *L) { lua_pushinteger(L, fx_list_sel(check_widget(L,1))); return 1; }
static int l_fx_list_set_cb(lua_State *L)
{
    fx_widget_t *w = check_widget(L, 1);
    if (lua_isnoneornil(L, 2)) { clear_widget_cb(L, w); fx_list_set_cb(w, NULL); return 0; }
    luaL_checktype(L, 2, LUA_TFUNCTION);
    push_cb_table(L);
    lua_pushlightuserdata(L, w);
    lua_pushvalue(L, 2);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    fx_list_set_cb(w, lua_list_cb);
    return 0;
}
static int l_fx_drop_new(lua_State *L)
{
    push_widget(L, fx_drop_new(check_str(L,1), check_str(L,2)));
    return 1;
}
static int l_fx_drop_new_p(lua_State *L)
{
    push_widget(L, fx_drop_new_p(check_str(L,1), check_str(L,2), check_int(L,3)));
    return 1;
}
static int l_fx_drop_add(lua_State *L) { fx_drop_add(check_widget(L,1), check_str(L,2)); return 0; }

/* ---- scroll ---- */
static int l_fx_scroll_content(lua_State *L) { fx_scroll_content(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fx_scroll_update(lua_State *L) { lua_pushinteger(L, fx_scroll_update(check_widget(L,1), check_int(L,2))); return 1; }
static int l_fx_set_scroll(lua_State *L) { fx_set_scroll(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fx_scrollbar_draw(lua_State *L) { fx_scrollbar_draw(check_widget(L,1), check_int(L,2), check_int(L,3)); return 0; }
static int l_fx_wheel_take(lua_State *L) { lua_pushinteger(L, fx_wheel_take(check_widget(L,1))); return 1; }

/* ---- canvas 状态 ---- */
static int l_fx_canvas_size(lua_State *L)
{
    int w = 0, h = 0;
    fx_canvas_size(check_widget(L,1), &w, &h);
    lua_pushinteger(L, w); lua_pushinteger(L, h);
    return 2;
}
static int l_fx_canvas_enable_buf(lua_State *L) { lua_pushinteger(L, fx_canvas_enable_buf(check_widget(L,1))); return 1; }
static int l_fx_canvas_set_buf(lua_State *L) { fx_canvas_set_buf(check_widget(L,1), check_int(L,2)); return 0; }
static int l_fx_canvas_clear(lua_State *L) { fx_canvas_clear(check_widget(L,1), check_color(L,2)); return 0; }
static int l_fx_canvas_begin(lua_State *L) { fx_canvas_begin(check_widget(L,1)); return 0; }
static int l_fx_canvas_end(lua_State *L) { (void)L; fx_canvas_end(); return 0; }

/* ==================== 绘制 API ==================== */

static int l_fx_set_color(lua_State *L) { fx_set_color(check_color(L,1)); return 0; }
static int l_fx_set_clip(lua_State *L) { fx_set_clip(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_reset_clip(lua_State *L) { (void)L; fx_reset_clip(); return 0; }
static int l_fx_draw_pixel(lua_State *L) { fx_draw_pixel(check_int(L,1),check_int(L,2)); return 0; }
static int l_fx_draw_hline(lua_State *L) { fx_draw_hline(check_int(L,1),check_int(L,2),check_int(L,3)); return 0; }
static int l_fx_draw_vline(lua_State *L) { fx_draw_vline(check_int(L,1),check_int(L,2),check_int(L,3)); return 0; }
static int l_fx_draw_line(lua_State *L) { fx_draw_line(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_draw_rect(lua_State *L) { fx_draw_rect(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_fill_rect(lua_State *L) { fx_fill_rect(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_fill_rect_gradient(lua_State *L)
{
    fx_fill_rect_gradient(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),
                          check_color(L,5), check_color(L,6), check_int(L,7));
    return 0;
}
static int l_fx_draw_rect_round(lua_State *L) { fx_draw_rect_round(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_fill_rect_round(lua_State *L) { fx_fill_rect_round(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_draw_circle(lua_State *L) { fx_draw_circle(check_int(L,1),check_int(L,2),check_int(L,3)); return 0; }
static int l_fx_fill_circle(lua_State *L) { fx_fill_circle(check_int(L,1),check_int(L,2),check_int(L,3)); return 0; }
static int l_fx_draw_ellipse(lua_State *L) { fx_draw_ellipse(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_fill_ellipse(lua_State *L) { fx_fill_ellipse(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4)); return 0; }
static int l_fx_draw_arc(lua_State *L) { fx_draw_arc(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_fill_arc(lua_State *L) { fx_fill_arc(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_draw_triangle(lua_State *L) { fx_draw_triangle(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5),check_int(L,6)); return 0; }
static int l_fx_fill_triangle(lua_State *L) { fx_fill_triangle(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5),check_int(L,6)); return 0; }

/* 点表：支持 {x1,y1,x2,y2,...} 平铺数组（与 C 的 int16_t pts[] 一致） */
static int read_points(lua_State *L, int idx, int16_t *pts, int cap)
{
    luaL_checktype(L, idx, LUA_TTABLE);
    int n = (int)lua_rawlen(L, idx);
    if (n > cap * 2) n = cap * 2;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, idx, i);
        pts[i-1] = (int16_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    return n / 2;
}

static int l_fx_draw_polygon(lua_State *L)
{
    int16_t pts[256];
    int n = read_points(L, 1, pts, 256);
    fx_draw_polygon(pts, n);
    return 0;
}
static int l_fx_fill_polygon(lua_State *L)
{
    int16_t pts[256];
    int n = read_points(L, 1, pts, 256);
    fx_fill_polygon(pts, n);
    return 0;
}
static int l_fx_fill_polygon_rot(lua_State *L)
{
    int16_t pts[256];
    int n = read_points(L, 1, pts, 256);
    fx_fill_polygon_rot(pts, n, check_int(L,2), check_int(L,3), check_int(L,4));
    return 0;
}
static int l_fx_fill_quad(lua_State *L)
{
    float xy[8];
    luaL_checktype(L, 1, LUA_TTABLE);
    for (int i = 0; i < 8; i++) { lua_rawgeti(L, 1, i+1); xy[i] = check_num(L, -1); lua_pop(L, 1); }
    fx_fill_quad(xy);
    return 0;
}
static int l_fx_draw_text(lua_State *L) { fx_draw_text(check_int(L,1),check_int(L,2),check_str(L,3)); return 0; }
static int l_fx_draw_text_c(lua_State *L) { fx_draw_text_c(check_int(L,1),check_int(L,2),check_str(L,3),check_color(L,4),check_color(L,5)); return 0; }
static int l_fx_draw_text_c_n(lua_State *L) { fx_draw_text_c_n(check_int(L,1),check_int(L,2),check_str(L,3),check_int(L,4),check_color(L,5),check_color(L,6)); return 0; }
static int l_fx_text_width(lua_State *L) { lua_pushinteger(L, fx_text_width(check_str(L,1))); return 1; }
static int l_fx_text_width_n(lua_State *L) { lua_pushinteger(L, fx_text_width_n(check_str(L,1), check_int(L,2))); return 1; }
static int l_fxtk_text_width_size(lua_State *L) { lua_pushinteger(L, fxtk_text_width_size(check_int(L,1), check_str(L,2))); return 1; }
static int l_fxtk_draw_text_size(lua_State *L) { fxtk_draw_text_size(check_int(L,1),check_int(L,2),check_int(L,3),check_str(L,4),check_color(L,5),check_color(L,6)); return 0; }
static int l_fxtk_font_set_size(lua_State *L) { fxtk_font_set_size(check_int(L,1)); return 0; }

static int l_fx_draw_image(lua_State *L) { fx_draw_image(check_image(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_draw_image_ex(lua_State *L) { fx_draw_image_ex(check_image(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5),check_int(L,6)); return 0; }
static int l_fx_draw_image_rot(lua_State *L) { fx_draw_image_rot(check_image(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_draw_image_quad(lua_State *L)
{
    float xy[8];
    fx_image_t *img = check_image(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    for (int i = 0; i < 8; i++) { lua_rawgeti(L, 2, i+1); xy[i] = check_num(L, -1); lua_pop(L, 1); }
    fx_draw_image_quad(img, xy);
    return 0;
}
static int l_fx_draw_image_quad_persp(lua_State *L)
{
    fx_draw_image_quad_persp(check_image(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5),
                             check_num(L,6), check_num(L,7));
    return 0;
}
static int l_fx_set_image_effect_flip_x(lua_State *L) { fx_image_flip_x(check_image(L,1)); return 0; }
static int l_fx_set_image_effect_flip_y(lua_State *L) { fx_image_flip_y(check_image(L,1)); return 0; }
static int l_fx_set_image_effect_gray(lua_State *L)  { fx_image_grayscale(check_image(L,1)); return 0; }
static int l_fx_set_image_effect_tint(lua_State *L)  { fx_image_tint(check_image(L,1), check_color(L,2), check_int(L,3)); return 0; }
static int l_fx_set_image_effect_bright(lua_State *L){ fx_image_brightness(check_image(L,1), check_int(L,2)); return 0; }

/* ---- 变换栈 ---- */
static int l_fx_canvas_push_affine(lua_State *L) { fx_canvas_push_affine(check_num(L,1),check_num(L,2),check_num(L,3),check_num(L,4),check_num(L,5),check_num(L,6)); return 0; }
static int l_fx_canvas_pop_affine(lua_State *L) { (void)L; fx_canvas_pop_affine(); return 0; }
static int l_fx_transform_reset(lua_State *L) { (void)L; fx_transform_reset(); return 0; }
static int l_fx_transform_depth(lua_State *L) { lua_pushinteger(L, fx_transform_depth()); return 1; }
static int l_fx_canvas_transform_point(lua_State *L)
{
    float ox = 0, oy = 0;
    fx_canvas_transform_point(check_num(L,1), check_num(L,2), &ox, &oy);
    lua_pushnumber(L, ox); lua_pushnumber(L, oy);
    return 2;
}
static int l_fx_draw_raymarch(lua_State *L) { fx_draw_raymarch(check_num(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_raymarch_available(lua_State *L) { lua_pushinteger(L, fx_raymarch_available()); return 1; }
static int l_fx_quad_warp_gpu(lua_State *L) { lua_pushinteger(L, fx_quad_warp_gpu()); return 1; }

/* ==================== 图像对象 ==================== */

static int l_fx_image_create(lua_State *L)
{
    push_image(L, fx_image_create(check_int(L,1), check_int(L,2)));
    return 1;
}
static int l_fx_image_load(lua_State *L)
{
    push_image(L, fx_image_load(check_str(L,1)));
    return 1;
}
static int l_fx_image_free(lua_State *L)
{
    lua_image_t *im = (lua_image_t *)luaL_checkudata(L, 1, FXTK_IMAGE_MT);
    if (im->img) { fx_image_free(im->img); im->img = NULL; }
    return 0;
}
static int l_fx_image_set_px(lua_State *L) { fx_image_set_px(check_image(L,1),check_int(L,2),check_int(L,3),check_color(L,4)); return 0; }
static int l_fx_image_size(lua_State *L)
{
    fx_image_t *im = check_image(L, 1);
    lua_pushinteger(L, im->w); lua_pushinteger(L, im->h);
    return 2;
}
static int l_fx_image_get_px(lua_State *L)
{
    fx_image_t *im = check_image(L, 1);
    int x = check_int(L,2), y = check_int(L,3);
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)im->px[y * im->w + x]);
    return 1;
}
static int l_fx_image_fill(lua_State *L)
{
    fx_image_t *im = check_image(L, 1);
    fx_color_t c = check_color(L, 2);
    for (int i = 0, n = (int)im->w * (int)im->h; i < n; i++) im->px[i] = c;
    return 0;
}
/* Lua 侧便利函数：避免在 Lua 里逐像素刷整屏。 */
static int l_fx_image_line(lua_State *L)
{
    fx_image_t *im = check_image(L, 1);
    int x0 = check_int(L,2), y0 = check_int(L,3), x1 = check_int(L,4), y1 = check_int(L,5);
    fx_color_t c = check_color(L, 6);
    int r = luaL_optinteger(L, 7, 0);
    int dx = abs(x1-x0), dy = abs(y1-y0);
    int n = (dx > dy ? dx : dy) + 1;
    for (int i = 0; i < n; i++) {
        int x = x0 + (x1-x0) * i / n;
        int y = y0 + (y1-y0) * i / n;
        for (int oy = -r; oy <= r; oy++)
            for (int ox = -r; ox <= r; ox++)
                fx_image_set_px(im, x+ox, y+oy, c);
    }
    return 0;
}

/* ==================== 设计稿 / 窗口 / 主题 ==================== */

static int l_fx_set_design_pixel(lua_State *L) { fx_set_design_pixel(check_int(L,1), check_int(L,2)); return 0; }
static int l_fx_get_design_pixel(lua_State *L)
{
    int w = 0, h = 0;
    fx_get_design_pixel(&w, &h);
    lua_pushinteger(L, w); lua_pushinteger(L, h);
    return 2;
}
static int l_fx_set_bg(lua_State *L) { fx_set_bg(check_color(L,1)); return 0; }
static int l_fx_get_bg(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_get_bg()); return 1; }
static int l_fx_set_global_background(lua_State *L) { fx_set_global_background(check_colorx(L,1)); return 0; }
static int l_fx_set_dark_theme(lua_State *L) { fx_set_dark_theme(check_int(L,1)); return 0; }
static int l_fx_is_dark_theme(lua_State *L) { lua_pushinteger(L, fx_is_dark_theme()); return 1; }
static int l_fx_colorx_current(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_colorx_current(check_colorx(L,1))); return 1; }
static int l_fx_set_window_title(lua_State *L) { fx_set_window_title(check_str(L,1)); return 0; }
static int l_fx_set_grid_lines(lua_State *L) { fx_set_grid_lines(check_int(L,1)); return 0; }
static int l_fxtk_grid_lines_on(lua_State *L) { lua_pushinteger(L, fxtk_grid_lines_on()); return 1; }
static int l_fxtk_set_fps_debug(lua_State *L) { fxtk_set_fps_debug(check_int(L,1)); return 0; }
static int l_fx_set_ime_pos(lua_State *L) { fx_set_ime_pos(check_int(L,1), check_int(L,2)); return 0; }
static int l_fx_set_touch_debug(lua_State *L) { fx_set_touch_debug(check_int(L,1)); return 0; }
static int l_fx_set_autorepaint(lua_State *L) { fx_set_autorepaint(check_int(L,1)); return 0; }
static int l_fx_animation(lua_State *L) { fx_animation(check_int(L,1)); return 0; }
static int l_fx_animation_enabled(lua_State *L) { lua_pushinteger(L, fx_animation_enabled()); return 1; }
static int l_fx_set_aa(lua_State *L) { fx_set_aa(check_int(L,1)); return 0; }
static int l_fx_set_widget_aa(lua_State *L) { fx_set_widget_aa(check_int(L,1)); return 0; }
static int l_fx_widget_aa_level(lua_State *L) { lua_pushinteger(L, fx_widget_aa_level()); return 1; }
static int l_fxtk_aa(lua_State *L) { lua_pushinteger(L, fxtk_aa()); return 1; }
static int l_fx_aa_autodegrade(lua_State *L) { lua_pushinteger(L, fx_aa_autodegrade()); return 1; }
static int l_fxtk_widget_count(lua_State *L) { lua_pushinteger(L, fxtk_widget_count()); return 1; }
static int l_fxtk_fps(lua_State *L) { lua_pushinteger(L, fxtk_fps()); return 1; }
static int l_fx_screenshot(lua_State *L) { lua_pushinteger(L, fx_screenshot(check_str(L,1))); return 1; }
static int l_fx_width(lua_State *L) { lua_pushinteger(L, fx_width()); return 1; }
static int l_fx_height(lua_State *L) { lua_pushinteger(L, fx_height()); return 1; }
static int l_fx_screen_w(lua_State *L) { lua_pushinteger(L, fx_screen_w()); return 1; }
static int l_fx_screen_h(lua_State *L) { lua_pushinteger(L, fx_screen_h()); return 1; }
static int l_fx_dpi_scale(lua_State *L) { lua_pushnumber(L, fx_dpi_scale()); return 1; }
static int l_fxtk_ui_scale(lua_State *L) { lua_pushinteger(L, fxtk_ui_scale()); return 1; }
static int l_fxtk_drv_width(lua_State *L) { lua_pushinteger(L, fxtk_drv_width()); return 1; }
static int l_fxtk_drv_height(lua_State *L) { lua_pushinteger(L, fxtk_drv_height()); return 1; }
static int l_fx_band_index(lua_State *L) { lua_pushinteger(L, fx_band_index()); return 1; }
static int l_fx_cpu_count(lua_State *L) { lua_pushinteger(L, fx_cpu_count()); return 1; }

/* 输入（供无头测试/自定义输入源） */
static int l_fx_touch_press(lua_State *L) { fx_touch_press(check_int(L,1), check_int(L,2)); return 0; }
static int l_fx_touch_release(lua_State *L) { fx_touch_release(check_int(L,1), check_int(L,2)); return 0; }
static int l_fx_touch_move(lua_State *L) { fx_touch_move(check_int(L,1), check_int(L,2)); return 0; }

static int l_fx_last_key(lua_State *L)
{
    fx_keyev_t k = fx_last_key();
    lua_newtable(L);
    lua_pushstring(L, k.utf8);  lua_setfield(L, -2, "utf8");
    lua_pushinteger(L, k.key);  lua_setfield(L, -2, "key");
    lua_pushinteger(L, k.down); lua_setfield(L, -2, "down");
    lua_pushinteger(L, k.mod);  lua_setfield(L, -2, "mod");
    return 1;
}

/* ==================== 后端服务 ==================== */

static int l_fx_time_ms(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_time_ms()); return 1; }
static int l_fx_time_us(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_time_us()); return 1; }
static int l_fx_time_unix(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_time_unix()); return 1; }
static int l_fx_sleep_ms(lua_State *L) { fx_sleep_ms(check_int(L,1)); return 0; }
static int l_fx_rand_seed(lua_State *L) { fx_rand_seed((uint64_t)luaL_checkinteger(L,1)); return 0; }
static int l_fx_rand_u32(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_rand_u32()); return 1; }
static int l_fx_rand_below(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_rand_below((uint32_t)luaL_checkinteger(L,1))); return 1; }
static int l_fx_rand_range(lua_State *L) { lua_pushinteger(L, fx_rand_range(check_int(L,1), check_int(L,2))); return 1; }
static int l_fx_randf(lua_State *L) { lua_pushnumber(L, fx_randf()); return 1; }

static int l_fx_file_exists(lua_State *L) { lua_pushboolean(L, fx_file_exists(check_str(L,1))); return 1; }
static int l_fx_file_size(lua_State *L)
{
    long long sz = 0;
    if (fx_file_size(check_str(L,1), &sz)) lua_pushinteger(L, (lua_Integer)sz);
    else lua_pushnil(L);
    return 1;
}
static int l_fx_file_read(lua_State *L)
{
    int n = 0;
    void *buf = fx_file_read(check_str(L,1), &n);
    if (!buf) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char *)buf, (size_t)(n > 0 ? n : 0));
    fx_file_free(buf);
    return 1;
}
static int l_fx_file_write(lua_State *L)
{
    size_t n = 0;
    const char *data = luaL_checklstring(L, 2, &n);
    lua_pushinteger(L, fx_file_write(check_str(L,1), data, (int)n, lua_toboolean(L,3)));
    return 1;
}
static int l_fx_dir_create(lua_State *L) { lua_pushinteger(L, fx_dir_create(check_str(L,1))); return 1; }
static int l_fx_dir_list(lua_State *L)
{
    int max = luaL_optinteger(L, 2, 256);
    if (max < 1) max = 1;
    if (max > 4096) max = 4096;
    fx_dir_entry_t *ents = (fx_dir_entry_t *)malloc((size_t)max * sizeof(fx_dir_entry_t));
    if (!ents) { lua_pushnil(L); return 1; }
    int n = fx_dir_list(check_str(L,1), ents, max);
    if (n < 0) { free(ents); lua_pushnil(L); return 1; }
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, ents[i].name); lua_setfield(L, -2, "name");
        lua_pushboolean(L, ents[i].is_dir); lua_setfield(L, -2, "is_dir");
        lua_pushinteger(L, (lua_Integer)ents[i].size); lua_setfield(L, -2, "size");
        lua_pushstring(L, ents[i].date); lua_setfield(L, -2, "date");
        lua_rawseti(L, -2, i + 1);
    }
    free(ents);
    return 1;
}
static int l_fx_path_join(lua_State *L)
{
    char buf[4096];
    int n = fx_path_join(buf, (int)sizeof(buf), check_str(L,1), check_str(L,2));
    if (n <= 0) lua_pushnil(L); else lua_pushlstring(L, buf, (size_t)n);
    return 1;
}
static int l_fx_path_basename(lua_State *L) { lua_pushstring(L, fx_path_basename(check_str(L,1))); return 1; }
static int l_fx_path_ext(lua_State *L) { lua_pushstring(L, fx_path_ext(check_str(L,1))); return 1; }
static int l_fx_path_dir(lua_State *L)
{
    char buf[4096];
    int n = fx_path_dir(buf, (int)sizeof(buf), check_str(L,1));
    if (n <= 0) lua_pushnil(L); else lua_pushlstring(L, buf, (size_t)n);
    return 1;
}
static int l_fx_dir_app(lua_State *L)
{
    char buf[4096];
    int ok = fx_dir_app(buf, (int)sizeof(buf));
    if (!ok) lua_pushnil(L); else lua_pushstring(L, buf);
    return 1;
}
static int l_fx_clip_set(lua_State *L) { lua_pushinteger(L, fx_clip_set(check_str(L,1))); return 1; }
static int l_fx_clip_get(lua_State *L)
{
    char buf[8192];
    int n = fx_clip_get(buf, (int)sizeof(buf));
    if (n <= 0) lua_pushnil(L); else lua_pushlstring(L, buf, (size_t)n);
    return 1;
}
static int l_fx_prefs_set(lua_State *L) { lua_pushinteger(L, fx_prefs_set(check_str(L,1), check_str(L,2))); return 1; }
static int l_fx_prefs_get(lua_State *L)
{
    char buf[4096];
    int n = fx_prefs_get(check_str(L,1), buf, (int)sizeof(buf));
    if (n <= 0) lua_pushnil(L); else lua_pushlstring(L, buf, (size_t)n);
    return 1;
}
static int l_fx_prefs_save(lua_State *L) { lua_pushinteger(L, fx_prefs_save()); return 1; }
static int l_fx_prefs_load(lua_State *L) { lua_pushinteger(L, fx_prefs_load()); return 1; }
static int l_fx_backend_pick_dir(lua_State *L)
{
    char buf[4096];
    if (fx_backend_pick_dir(buf, (int)sizeof(buf))) lua_pushstring(L, buf);
    else lua_pushnil(L);
    return 1;
}
static int l_fx_backend_pick_file(lua_State *L)
{
    char buf[4096];
    const char *title = lua_isnoneornil(L,1) ? NULL : luaL_checkstring(L,1);
    const char *ext   = lua_isnoneornil(L,2) ? NULL : luaL_checkstring(L,2);
    if (fx_backend_pick_file(buf, (int)sizeof(buf), title, ext)) lua_pushstring(L, buf);
    else lua_pushnil(L);
    return 1;
}
static int l_fx_open_url(lua_State *L) { lua_pushinteger(L, fx_open_url(check_str(L,1))); return 1; }
static int l_fx_reveal_file(lua_State *L) { lua_pushinteger(L, fx_reveal_file(check_str(L,1))); return 1; }
static int l_fx_backend_name(lua_State *L) { lua_pushstring(L, fx_backend_name()); return 1; }
static int l_fx_backend_caps(lua_State *L)
{
    fx_caps_t c = fx_backend_caps();
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, c.has_gpu);    lua_setfield(L, -2, "has_gpu");
    lua_pushinteger(L, c.gpu_aa);     lua_setfield(L, -2, "gpu_aa");
    lua_pushinteger(L, c.quad_warp);  lua_setfield(L, -2, "quad_warp");
    lua_pushinteger(L, c.clipboard);  lua_setfield(L, -2, "clipboard");
    lua_pushinteger(L, c.file_dialog);lua_setfield(L, -2, "file_dialog");
    lua_pushinteger(L, c.max_texture);lua_setfield(L, -2, "max_texture");
    lua_pushstring(L, c.platform);    lua_setfield(L, -2, "platform");
    return 1;
}
static int l_fx_log_set_level(lua_State *L) { fx_log_set_level((fx_log_level_t)check_int(L,1)); return 0; }
static int l_fx_log_get_level(lua_State *L) { lua_pushinteger(L, (lua_Integer)fx_log_get_level()); return 1; }
static int l_fx_log(lua_State *L)
{
    int lv = check_int(L, 1);
    const char *msg = check_str(L, 2);
    fx_log((fx_log_level_t)lv, "%s", msg);
    return 0;
}

/* ==================== 弹性布局 / 手势 / SVG / 窗口 ==================== */

static int l_fx_flex_apply_simple(lua_State *L)
{
    fx_flex_apply_simple(check_widget(L,1), check_int(L,2), check_int(L,3), check_int(L,4),
                         check_int(L,5), check_int(L,6));
    return 0;
}
static int l_fx_flex_apply(lua_State *L)
{
    fx_widget_t *w = check_widget(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int n = (int)lua_rawlen(L, 2);
    if (n > 64) n = 64;
    fx_flex_item_t items[64];
    memset(items, 0, sizeof(items));
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 2, i+1);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "size");        items[i].size = (int)lua_tointeger(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "grow");        items[i].grow = (int)lua_tointeger(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "min_size");    items[i].min_size = (int)lua_tointeger(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "max_size");    items[i].max_size = (int)lua_tointeger(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "cross_size");  items[i].cross_size = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    fx_flex_apply(w, items, n, check_int(L,3), check_int(L,4), check_int(L,5),
                  check_int(L,6), check_int(L,7));
    return 0;
}

static int l_fx_gesture_poll(lua_State *L)
{
    fx_gesture_ev_t ev;
    if (!fx_gesture_poll(&ev)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, (lua_Integer)ev.type);       lua_setfield(L, -2, "type");
    lua_pushinteger(L, ev.x);                       lua_setfield(L, -2, "x");
    lua_pushinteger(L, ev.y);                       lua_setfield(L, -2, "y");
    lua_pushinteger(L, ev.dx);                      lua_setfield(L, -2, "dx");
    lua_pushinteger(L, ev.dy);                      lua_setfield(L, -2, "dy");
    lua_pushinteger(L, ev.duration_ms);             lua_setfield(L, -2, "duration_ms");
    lua_pushstring(L, fx_gesture_name(ev.type));    lua_setfield(L, -2, "name");
    return 1;
}
static int l_fx_gesture_pending(lua_State *L) { lua_pushinteger(L, fx_gesture_pending()); return 1; }
static int l_fx_gesture_active(lua_State *L) { lua_pushinteger(L, fx_gesture_active()); return 1; }
static int l_fx_gesture_dropped(lua_State *L) { lua_pushinteger(L, fx_gesture_dropped()); return 1; }
static int l_fx_gesture_reset(lua_State *L) { (void)L; fx_gesture_reset(); return 0; }
static int l_fx_gesture_config(lua_State *L) { fx_gesture_config(check_int(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_gesture_config_default(lua_State *L) { (void)L; fx_gesture_config_default(); return 0; }
static int l_fx_gesture_feed(lua_State *L) { fx_gesture_feed(check_int(L,1),check_int(L,2),check_int(L,3),(uint32_t)luaL_checkinteger(L,4)); return 0; }
static int l_fx_gesture_name(lua_State *L) { lua_pushstring(L, fx_gesture_name(check_int(L,1))); return 1; }

static int l_fx_svg_parse(lua_State *L) { push_svg(L, fx_svg_parse(check_str(L,1))); return 1; }
static int l_fx_svg_load(lua_State *L) { push_svg(L, fx_svg_load(check_str(L,1))); return 1; }
static int l_fx_svg_free(lua_State *L)
{
    lua_svg_t *s = (lua_svg_t *)luaL_checkudata(L, 1, FXTK_SVG_MT);
    if (s->svg) { fx_svg_free(s->svg); s->svg = NULL; }
    return 0;
}
static int l_fx_svg_draw(lua_State *L) { fx_svg_draw(check_svg(L,1),check_int(L,2),check_int(L,3),check_int(L,4),check_int(L,5)); return 0; }
static int l_fx_svg_shape_count(lua_State *L) { lua_pushinteger(L, fx_svg_shape_count(check_svg(L,1))); return 1; }
static int l_fx_svg_shape_points(lua_State *L) { lua_pushinteger(L, fx_svg_shape_points(check_svg(L,1), check_int(L,2))); return 1; }
static int l_fx_svg_shape_bbox(lua_State *L)
{
    float x1, y1, x2, y2;
    if (!fx_svg_shape_bbox(check_svg(L,1), check_int(L,2), &x1, &y1, &x2, &y2)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L,x1); lua_pushnumber(L,y1); lua_pushnumber(L,x2); lua_pushnumber(L,y2);
    return 4;
}
static int l_fx_svg_skipped(lua_State *L) { lua_pushinteger(L, fx_svg_skipped(check_svg(L,1))); return 1; }
static int l_fx_svg_size(lua_State *L)
{
    float w = 0, h = 0;
    fx_svg_size(check_svg(L,1), &w, &h);
    lua_pushnumber(L, w); lua_pushnumber(L, h);
    return 2;
}

/* 单窗口桌面行为只在 sokol/原生窗口后端有意义；SDL dummy 无头宿主用
 * -DFXTK_LUA_NO_WINAPI 编译，保留同名 Lua 函数但一律返回“不支持”，
 * 这样同一份 Lua 脚本在两种宿主下都不会因为缺符号而链接失败。 */
#ifndef FXTK_LUA_NO_WINAPI
static int l_fx_win_available(lua_State *L) { lua_pushinteger(L, fx_win_available()); return 1; }
static int l_fx_win_set_borderless(lua_State *L) { lua_pushinteger(L, fx_win_set_borderless(check_int(L,1))); return 1; }
static int l_fx_win_get_borderless(lua_State *L) { lua_pushinteger(L, fx_win_get_borderless()); return 1; }
static int l_fx_win_set_always_on_top(lua_State *L) { lua_pushinteger(L, fx_win_set_always_on_top(check_int(L,1))); return 1; }
static int l_fx_win_get_always_on_top(lua_State *L) { lua_pushinteger(L, fx_win_get_always_on_top()); return 1; }
static int l_fx_win_get_decorated(lua_State *L) { lua_pushinteger(L, fx_win_get_decorated()); return 1; }
static int l_fx_win_move(lua_State *L) { lua_pushinteger(L, fx_win_move(check_int(L,1), check_int(L,2))); return 1; }
static int l_fx_win_get_pos(lua_State *L)
{
    int x = 0, y = 0;
    if (!fx_win_get_pos(&x, &y)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, x); lua_pushinteger(L, y);
    return 2;
}
static int l_fx_win_center(lua_State *L) { lua_pushinteger(L, fx_win_center()); return 1; }
static int l_fx_win_drag_begin(lua_State *L) { (void)L; fx_win_drag_begin(); return 0; }
#else
static int l_fx_win_available(lua_State *L)            { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_set_borderless(lua_State *L)      { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_get_borderless(lua_State *L)      { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_set_always_on_top(lua_State *L)   { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_get_always_on_top(lua_State *L)   { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_get_decorated(lua_State *L)       { (void)L; lua_pushinteger(L, -1); return 1; }
static int l_fx_win_move(lua_State *L)                { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_get_pos(lua_State *L)             { (void)L; lua_pushnil(L); return 1; }
static int l_fx_win_center(lua_State *L)              { (void)L; lua_pushinteger(L, 0); return 1; }
static int l_fx_win_drag_begin(lua_State *L)          { (void)L; return 0; }
#endif

static int l_fx_parallel_threads(lua_State *L) { lua_pushinteger(L, fx_parallel_threads()); return 1; }
static int l_fx_parallel_set_threads(lua_State *L) { fx_parallel_set_threads(check_int(L,1)); return 0; }

static int l_fx_widget_fix(lua_State *L) { fx_widget_fix(check_widget(L,1), check_int(L,2), check_int(L,3)); return 0; }
static int l_fx_color_alpha(lua_State *L) { lua_pushinteger(L, fx_color_alpha(check_color(L,1))); return 1; }

/* ==================== 注册表 ==================== */

static const luaL_Reg fxtk_core_funcs[] = {
    /* 构造器 */
    {"fx_button_new", l_fx_button_new},
    {"fx_label_new", l_fx_label_new},
    {"fx_grid_map", l_fx_grid_map},
    {"fx_canvas_new", l_fx_canvas_new},
    {"fx_slider_new", l_fx_slider_new},
    {"fx_progress_new", l_fx_progress_new},
    {"fx_checkbox_new", l_fx_checkbox_new},
    {"fx_panel_new", l_fx_panel_new},
    {"fx_tab_new", l_fx_tab_new},
    {"fx_textedit_new", l_fx_textedit_new},
    {"fx_scroll_new", l_fx_scroll_new},
    {"fx_image_new", l_fx_image_new},
    {"fx_widget_new", l_fx_widget_new},
    /* 控件 */
    {"fx_find", l_fx_find},
    {"fx_parent", l_fx_parent},
    {"fx_delete", l_fx_delete},
    {"fx_set_title", l_fx_set_title},
    {"fx_set_color_w", l_fx_set_color_w},
    {"fx_set_fgcolor_w", l_fx_set_fgcolor_w},
    {"fx_set_value", l_fx_set_value},
    {"fx_get_value", l_fx_get_value},
    {"fx_set_visible", l_fx_set_visible},
    {"fx_widget_type", l_fx_widget_type},
    {"fx_widget_title", l_fx_widget_title},
    {"fx_set_cb", l_fx_set_cb},
    {"fx_widget_set_rect", l_fx_widget_set_rect},
    {"fx_widget_rect", l_fx_widget_rect},
    {"fx_widget_fix", l_fx_widget_fix},
    {"fx_layout", l_fx_layout},
    {"fx_repaint", l_fx_repaint},
    {"fx_repaint_rect", l_fx_repaint_rect},
    {"fx_set_max_scale", l_fx_set_max_scale},
    {"fx_set_fit", l_fx_set_fit},
    {"fxtk_apply_fit", l_fxtk_apply_fit},
    {"fxtk_unlink", l_fxtk_unlink},
    {"fxtk_raise", l_fxtk_raise},
    {"fxtk_lower", l_fxtk_lower},
    {"fx_root_new", l_fx_root_new},
    {"fx_root_select", l_fx_root_select},
    {"fx_root_active", l_fx_root_active},
    {"fx_set_focus", l_fx_set_focus},
    {"fx_get_focus", l_fx_get_focus},
    {"fx_pressed", l_fx_pressed},
    {"fx_touch_state", l_fx_touch_state},
    {"fx_set_fontsize", l_fx_set_fontsize},
    {"fx_set_align", l_fx_set_align},
    {"fx_textedit_set_readonly", l_fx_textedit_set_readonly},
    {"fx_textedit_text", l_fx_textedit_text},
    {"fx_set_image", l_fx_set_image},
    {"fx_image_set_zoom", l_fx_image_set_zoom},
    /* list / drop / scroll */
    {"fx_list_new", l_fx_list_new},
    {"fx_list_new_p", l_fx_list_new_p},
    {"fx_list_add", l_fx_list_add},
    {"fx_list_clear", l_fx_list_clear},
    {"fx_list_sel", l_fx_list_sel},
    {"fx_list_set_cb", l_fx_list_set_cb},
    {"fx_drop_new", l_fx_drop_new},
    {"fx_drop_new_p", l_fx_drop_new_p},
    {"fx_drop_add", l_fx_drop_add},
    {"fx_scroll_content", l_fx_scroll_content},
    {"fx_scroll_update", l_fx_scroll_update},
    {"fx_set_scroll", l_fx_set_scroll},
    {"fx_scrollbar_draw", l_fx_scrollbar_draw},
    {"fx_wheel_take", l_fx_wheel_take},
    /* canvas */
    {"fx_canvas_size", l_fx_canvas_size},
    {"fx_canvas_enable_buf", l_fx_canvas_enable_buf},
    {"fx_canvas_set_buf", l_fx_canvas_set_buf},
    {"fx_canvas_clear", l_fx_canvas_clear},
    {"fx_canvas_begin", l_fx_canvas_begin},
    {"fx_canvas_end", l_fx_canvas_end},
    /* 绘制 */
    {"fx_set_color", l_fx_set_color},
    {"fx_set_clip", l_fx_set_clip},
    {"fx_reset_clip", l_fx_reset_clip},
    {"fx_draw_pixel", l_fx_draw_pixel},
    {"fx_draw_hline", l_fx_draw_hline},
    {"fx_draw_vline", l_fx_draw_vline},
    {"fx_draw_line", l_fx_draw_line},
    {"fx_draw_rect", l_fx_draw_rect},
    {"fx_fill_rect", l_fx_fill_rect},
    {"fx_fill_rect_gradient", l_fx_fill_rect_gradient},
    {"fx_draw_rect_round", l_fx_draw_rect_round},
    {"fx_fill_rect_round", l_fx_fill_rect_round},
    {"fx_draw_circle", l_fx_draw_circle},
    {"fx_fill_circle", l_fx_fill_circle},
    {"fx_draw_ellipse", l_fx_draw_ellipse},
    {"fx_fill_ellipse", l_fx_fill_ellipse},
    {"fx_draw_arc", l_fx_draw_arc},
    {"fx_fill_arc", l_fx_fill_arc},
    {"fx_draw_triangle", l_fx_draw_triangle},
    {"fx_fill_triangle", l_fx_fill_triangle},
    {"fx_draw_polygon", l_fx_draw_polygon},
    {"fx_fill_polygon", l_fx_fill_polygon},
    {"fx_fill_polygon_rot", l_fx_fill_polygon_rot},
    {"fx_fill_quad", l_fx_fill_quad},
    {"fx_draw_text", l_fx_draw_text},
    {"fx_draw_text_c", l_fx_draw_text_c},
    {"fx_draw_text_c_n", l_fx_draw_text_c_n},
    {"fx_text_width", l_fx_text_width},
    {"fx_text_width_n", l_fx_text_width_n},
    {"fxtk_text_width_size", l_fxtk_text_width_size},
    {"fxtk_draw_text_size", l_fxtk_draw_text_size},
    {"fxtk_font_set_size", l_fxtk_font_set_size},
    {"fx_draw_image", l_fx_draw_image},
    {"fx_draw_image_ex", l_fx_draw_image_ex},
    {"fx_draw_image_rot", l_fx_draw_image_rot},
    {"fx_draw_image_quad", l_fx_draw_image_quad},
    {"fx_draw_image_quad_persp", l_fx_draw_image_quad_persp},
    {"fx_image_flip_x", l_fx_set_image_effect_flip_x},
    {"fx_image_flip_y", l_fx_set_image_effect_flip_y},
    {"fx_image_grayscale", l_fx_set_image_effect_gray},
    {"fx_image_tint", l_fx_set_image_effect_tint},
    {"fx_image_brightness", l_fx_set_image_effect_bright},
    {"fx_canvas_push_affine", l_fx_canvas_push_affine},
    {"fx_canvas_pop_affine", l_fx_canvas_pop_affine},
    {"fx_transform_reset", l_fx_transform_reset},
    {"fx_transform_depth", l_fx_transform_depth},
    {"fx_canvas_transform_point", l_fx_canvas_transform_point},
    {"fx_draw_raymarch", l_fx_draw_raymarch},
    {"fx_raymarch_available", l_fx_raymarch_available},
    {"fx_quad_warp_gpu", l_fx_quad_warp_gpu},
    /* 图像对象 */
    {"fx_image_create", l_fx_image_create},
    {"fx_image_load", l_fx_image_load},
    {"fx_image_free", l_fx_image_free},
    {"fx_image_set_px", l_fx_image_set_px},
    {"fx_image_size", l_fx_image_size},
    {"fx_image_get_px", l_fx_image_get_px},
    {"fx_image_fill", l_fx_image_fill},
    {"fx_image_line", l_fx_image_line},
    /* 设计稿 / 窗口 / 主题 */
    {"fx_set_design_pixel", l_fx_set_design_pixel},
    {"fx_get_design_pixel", l_fx_get_design_pixel},
    {"fx_set_bg", l_fx_set_bg},
    {"fx_get_bg", l_fx_get_bg},
    {"fx_set_global_background", l_fx_set_global_background},
    {"fx_set_dark_theme", l_fx_set_dark_theme},
    {"fx_is_dark_theme", l_fx_is_dark_theme},
    {"fx_colorx_current", l_fx_colorx_current},
    {"fx_set_window_title", l_fx_set_window_title},
    {"fx_set_grid_lines", l_fx_set_grid_lines},
    {"fxtk_grid_lines_on", l_fxtk_grid_lines_on},
    {"fxtk_set_fps_debug", l_fxtk_set_fps_debug},
    {"fx_set_ime_pos", l_fx_set_ime_pos},
    {"fx_set_touch_debug", l_fx_set_touch_debug},
    {"fx_set_autorepaint", l_fx_set_autorepaint},
    {"fx_animation", l_fx_animation},
    {"fx_animation_enabled", l_fx_animation_enabled},
    {"fx_set_aa", l_fx_set_aa},
    {"fx_set_widget_aa", l_fx_set_widget_aa},
    {"fx_widget_aa_level", l_fx_widget_aa_level},
    {"fxtk_aa", l_fxtk_aa},
    {"fx_aa_autodegrade", l_fx_aa_autodegrade},
    {"fxtk_widget_count", l_fxtk_widget_count},
    {"fxtk_fps", l_fxtk_fps},
    {"fx_screenshot", l_fx_screenshot},
    {"fx_width", l_fx_width},
    {"fx_height", l_fx_height},
    {"fx_screen_w", l_fx_screen_w},
    {"fx_screen_h", l_fx_screen_h},
    {"fx_dpi_scale", l_fx_dpi_scale},
    {"fxtk_ui_scale", l_fxtk_ui_scale},
    {"fxtk_drv_width", l_fxtk_drv_width},
    {"fxtk_drv_height", l_fxtk_drv_height},
    {"fx_band_index", l_fx_band_index},
    {"fx_cpu_count", l_fx_cpu_count},
    {"fx_color_alpha", l_fx_color_alpha},
    {"fx_touch_press", l_fx_touch_press},
    {"fx_touch_release", l_fx_touch_release},
    {"fx_touch_move", l_fx_touch_move},
    {"fx_last_key", l_fx_last_key},
    /* 后端 */
    {"fx_time_ms", l_fx_time_ms},
    {"fx_time_us", l_fx_time_us},
    {"fx_time_unix", l_fx_time_unix},
    {"fx_sleep_ms", l_fx_sleep_ms},
    {"fx_rand_seed", l_fx_rand_seed},
    {"fx_rand_u32", l_fx_rand_u32},
    {"fx_rand_below", l_fx_rand_below},
    {"fx_rand_range", l_fx_rand_range},
    {"fx_randf", l_fx_randf},
    {"fx_file_exists", l_fx_file_exists},
    {"fx_file_size", l_fx_file_size},
    {"fx_file_read", l_fx_file_read},
    {"fx_file_write", l_fx_file_write},
    {"fx_dir_create", l_fx_dir_create},
    {"fx_dir_list", l_fx_dir_list},
    {"fx_path_join", l_fx_path_join},
    {"fx_path_basename", l_fx_path_basename},
    {"fx_path_ext", l_fx_path_ext},
    {"fx_path_dir", l_fx_path_dir},
    {"fx_dir_app", l_fx_dir_app},
    {"fx_clip_set", l_fx_clip_set},
    {"fx_clip_get", l_fx_clip_get},
    {"fx_prefs_set", l_fx_prefs_set},
    {"fx_prefs_get", l_fx_prefs_get},
    {"fx_prefs_save", l_fx_prefs_save},
    {"fx_prefs_load", l_fx_prefs_load},
    {"fx_backend_pick_dir", l_fx_backend_pick_dir},
    {"fx_backend_pick_file", l_fx_backend_pick_file},
    {"fx_open_url", l_fx_open_url},
    {"fx_reveal_file", l_fx_reveal_file},
    {"fx_backend_name", l_fx_backend_name},
    {"fx_backend_caps", l_fx_backend_caps},
    {"fx_log_set_level", l_fx_log_set_level},
    {"fx_log_get_level", l_fx_log_get_level},
    {"fx_log", l_fx_log},
    /* flex / gesture / svg / window / par */
    {"fx_flex_apply_simple", l_fx_flex_apply_simple},
    {"fx_flex_apply", l_fx_flex_apply},
    {"fx_gesture_poll", l_fx_gesture_poll},
    {"fx_gesture_pending", l_fx_gesture_pending},
    {"fx_gesture_active", l_fx_gesture_active},
    {"fx_gesture_dropped", l_fx_gesture_dropped},
    {"fx_gesture_reset", l_fx_gesture_reset},
    {"fx_gesture_config", l_fx_gesture_config},
    {"fx_gesture_config_default", l_fx_gesture_config_default},
    {"fx_gesture_feed", l_fx_gesture_feed},
    {"fx_gesture_name", l_fx_gesture_name},
    {"fx_svg_parse", l_fx_svg_parse},
    {"fx_svg_load", l_fx_svg_load},
    {"fx_svg_free", l_fx_svg_free},
    {"fx_svg_draw", l_fx_svg_draw},
    {"fx_svg_shape_count", l_fx_svg_shape_count},
    {"fx_svg_shape_points", l_fx_svg_shape_points},
    {"fx_svg_shape_bbox", l_fx_svg_shape_bbox},
    {"fx_svg_skipped", l_fx_svg_skipped},
    {"fx_svg_size", l_fx_svg_size},
    {"fx_win_available", l_fx_win_available},
    {"fx_win_set_borderless", l_fx_win_set_borderless},
    {"fx_win_get_borderless", l_fx_win_get_borderless},
    {"fx_win_set_always_on_top", l_fx_win_set_always_on_top},
    {"fx_win_get_always_on_top", l_fx_win_get_always_on_top},
    {"fx_win_get_decorated", l_fx_win_get_decorated},
    {"fx_win_move", l_fx_win_move},
    {"fx_win_get_pos", l_fx_win_get_pos},
    {"fx_win_center", l_fx_win_center},
    {"fx_win_drag_begin", l_fx_win_drag_begin},
    {"fx_parallel_threads", l_fx_parallel_threads},
    {"fx_parallel_set_threads", l_fx_parallel_set_threads},
    {NULL, NULL}
};

int luaopen_fxtk_core(lua_State *L)
{
    g_L = L;

    luaL_newmetatable(L, FXTK_IMAGE_MT);
    lua_pushcfunction(L, l_image_gc);       lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_image_tostring); lua_setfield(L, -2, "__tostring");
    lua_pushliteral(L, "image");            lua_setfield(L, -2, "__name");
    lua_pop(L, 1);

    luaL_newmetatable(L, FXTK_SVG_MT);
    lua_pushcfunction(L, l_svg_gc);          lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "svg");               lua_setfield(L, -2, "__name");
    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, fxtk_core_funcs, 0);
    lua_pushliteral(L, FXTK_LUA_VERSION);
    lua_setfield(L, -2, "_VERSION");
    lua_pushliteral(L, "fxtk Lua binding (C core)");
    lua_setfield(L, -2, "_DESCRIPTION");
    lua_pushliteral(L, FXTK_VERSION);
    lua_setfield(L, -2, "_CORE_VERSION");
    return 1;
}
