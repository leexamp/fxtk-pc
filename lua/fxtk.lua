--[[
fxtk.lua — fxtk C 核心的 Lua 语法层（Lua 5.4）

目标：Lua 写法与 C 的属性宏语法尽量一致。

    require("fxtk").install_globals()

    function app_init()
        fx_label_new(pixel("16,12", "464,40"), title("设置"))
        fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"), call(on_ok))
    end

    function on_ok(w)
        fx_set_title(w, "已点击")
    end

不用 install_globals 时，也可以写成命名空间风格：

    local fx = require("fxtk")
    fx.fx_button_new(fx.pixel(...), fx.title("确定"), fx.call(on_ok))

说明：
  * 属性构造函数（pixel/title/call/...）在这里用纯 Lua 实现，返回带 __fx 标记的 table；
    C 绑定负责把这些 table 翻译成 fx_attr_t。
  * fx_* 函数来自 fxtk.core（C 模块），名字与 fxtk.h 保持一致。
  * out 参数（如 fx_canvas_size）在 Lua 里改成多返回值。
]]

local core = require("fxtk.core")

local M = {}
for k, v in pairs(core) do M[k] = v end

M._VERSION = core._VERSION or "0.1.0"
M._CORE = "fxtk-pc-v2.5"   -- C 核心版本标识；Lua 层只是绑定
M.fxtk_version = core._CORE_VERSION or "2.5.3"
M.fxtk_core = M._CORE

-- ==================== 颜色 ====================

function M.FX_RGB(r, g, b)
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)
end

function M.FX_RGBA(r, g, b, a)
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)
end

M.FX_TEXT_NOBG = 0x00FFFFFF

M.FX_RED     = 0xFF0000
M.FX_GREEN   = 0x00FF00
M.FX_BLUE    = 0x0000FF
M.FX_WHITE   = 0xFFFFFF
M.FX_BLACK   = 0x000000
M.FX_YELLOW  = 0xFFFF00
M.FX_CYAN    = 0x00FFFF
M.FX_MAGENTA = 0xFF00FF
M.FX_GRAY    = 0x848484
M.FX_LGRAY   = 0xC8C8C8

M.FX_WINDOW_BG   = M.FX_RGB(245, 245, 245)
M.FX_WINDOW_DARK = M.FX_RGB(30, 30, 34)
M.FX_UI_FG       = M.FX_RGB(40, 40, 40)
M.FX_BTN_BLUE    = M.FX_RGB(33, 150, 243)
M.FX_OK_GREEN    = M.FX_RGB(76, 175, 80)
M.FX_RED_ACCENT  = M.FX_RGB(244, 67, 54)
M.FX_PURPLE      = M.FX_RGB(156, 39, 176)
M.FX_CANVAS_DARK = M.FX_RGB(30, 30, 30)

-- ==================== 枚举（与 fxtk.h 同值） ====================

M.FX_W_NONE = 0
M.FX_W_BUTTON = 1
M.FX_W_LABEL = 2
M.FX_W_GRID = 3
M.FX_W_CANVAS = 4
M.FX_W_SLIDER = 5
M.FX_W_PROGRESS = 6
M.FX_W_CHECKBOX = 7
M.FX_W_PANEL = 8
M.FX_W_TAB = 9
M.FX_W_IMAGE = 16
M.FX_W_TEXTEDIT = 17
M.FX_W_SCROLL = 19

M.FX_TAB_TOP = 0
M.FX_TAB_LEFT = 1
M.FX_TAB_RIGHT = 2
M.FX_TAB_BOTTOM = 3

M.FX_POS_FIXED = 3
M.FX_POS_FLEX = 4
M.FX_F_DENSE = 1 << 7
M.FX_F_FIT = 1 << 8
M.FX_F_READONLY = 1 << 9

M.FX_FLEX_ROW = 0
M.FX_FLEX_COL = 1
M.FX_FLEX_START = 0
M.FX_FLEX_CENTER = 1
M.FX_FLEX_END = 2
M.FX_FLEX_SPACE_BETWEEN = 3
M.FX_FLEX_SPACE_AROUND = 4
M.FX_FLEX_ALIGN_START = 0
M.FX_FLEX_ALIGN_CENTER = 1
M.FX_FLEX_ALIGN_END = 2
M.FX_FLEX_ALIGN_STRETCH = 3

M.FX_GEST_NONE = 0
M.FX_GEST_TAP = 1
M.FX_GEST_DOUBLE_TAP = 2
M.FX_GEST_LONG_PRESS = 3
M.FX_GEST_DRAG_BEGIN = 4
M.FX_GEST_DRAG_MOVE = 5
M.FX_GEST_DRAG_END = 6
M.FX_GEST_SWIPE_LEFT = 7
M.FX_GEST_SWIPE_RIGHT = 8
M.FX_GEST_SWIPE_UP = 9
M.FX_GEST_SWIPE_DOWN = 10

M.FX_KEY_BACKSPACE = 8
M.FX_KEY_RETURN = 13
M.FX_KEY_ESCAPE = 27
M.FX_KEY_LEFT = 1
M.FX_KEY_RIGHT = 2
M.FX_KEY_HOME = 3
M.FX_KEY_END = 4
M.FX_KEY_UP = 5
M.FX_KEY_DOWN = 6
M.FX_KEY_DELETE = 127

M.FX_LOG_DEBUG = 0
M.FX_LOG_INFO = 1
M.FX_LOG_WARN = 2
M.FX_LOG_ERROR = 3

-- ==================== 属性构造器 ====================

local function attr(tag, t)
    t.__fx = tag
    return t
end

function M.pixel(a, b)
    assert(type(a) == "string" and type(b) == "string", "pixel() 需要两个 \"x,y\" 字符串")
    return attr("pixel", { a = a, b = b })
end

function M.percent(a, b)
    assert(type(a) == "string" and type(b) == "string", "percent() 需要两个 \"0.0~1.0\" 字符串")
    return attr("percent", { a = a, b = b })
end

-- grid("name") 或 grid("name", r1, c1, r2, c2)，与 C 的可变参数宏同义。
function M.grid(name, ...)
    assert(type(name) == "string", "grid() 第一个参数是网格控件名")
    local n = select("#", ...)
    if n == 0 then
        return attr("grid", { name = name, n = 0 })
    end
    local r1, c1, r2, c2 = ...
    assert(n == 4, "grid() 要么只给名字，要么给 name,r1,c1,r2,c2")
    return attr("grid", { name = name, n = 4, r1 = r1, c1 = c1, r2 = r2, c2 = c2 })
end

function M.title(s)  return attr("title",  { s = tostring(s) }) end
function M.text(s)   return attr("text",   { s = tostring(s) }) end
function M.name(s)   return attr("name",   { s = tostring(s) }) end
function M.call(fn)  return attr("call",   { fn = assert(fn, "call() 需要一个 Lua 函数") }) end
function M.line(n)   return attr("line",   { n = n }) end
function M.row(n)    return attr("row",    { n = n }) end
function M.color(c)  return attr("color",  { c = c }) end
function M.fgcolor(c)return attr("fgcolor",{ c = c }) end
function M.border(n) return attr("border", { n = n }) end
function M.radius(n) return attr("radius", { n = n }) end
function M.dense()   return attr("dense",  {}) end
function M.value(n)  return attr("value",  { n = n }) end
function M.page(n)   return attr("page",   { n = n }) end
function M.anim(n)   return attr("anim",   { n = n }) end
function M.sidebar(n)return attr("sidebar",{ n = n }) end
function M.maxlen(n) return attr("maxlen", { n = n }) end
function M.image(img)return attr("image",  { img = img }) end
function M.fx_wptr(w)return attr("widget", { w = w }) end

local ATTR = {
    pixel = true, percent = true, grid = true, title = true, text = true, name = true,
    call = true, line = true, row = true, color = true, fgcolor = true, border = true,
    radius = true, dense = true, value = true, page = true, anim = true, sidebar = true,
    maxlen = true, image = true, fx_wptr = true,
}

-- 把 C 风格的全局名字装进 _G（与 fxtk.h 的写法一致）：
--   require("fxtk").install_globals()
--   ... 之后可直接写 fx_button_new(pixel(...), title(...), call(fn)) ...
function M.install_globals()
    for k, v in pairs(M) do
        if type(k) == "string" then
            if k:match("^fx_") or k:match("^fxtk_") or k:match("^FX_") or ATTR[k] then
                _G[k] = v
            end
        end
    end
    _G.fx = M   -- 同时保留命名空间入口：fx.set_cb(...) / fx.install_globals() ...
    return M
end

return M
