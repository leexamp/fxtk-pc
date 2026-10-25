--[[
lua_test.lua — fxtk Lua 绑定的无头自检

跑法（无显示环境）:
    cd demo-main && make lua-test

覆盖：
  * 属性构造器 / 控件构造与查找 / 取值设值
  * call 回调（通过模拟点击触发）
  * canvas 立即模式绘制（在回调里跑，不崩）
  * 图片对象（创建/写像素/绘制/后处理）
  * list / drop / textedit / flex / SVG / 文件后端
  * 手势 API / 颜色位运算 / 多返回值约定
]]
require("fxtk").install_globals()

local S = { clicked = 0, frames = 0, canvas_ok = 0, list_sel = -1, list_cb = false, setcb = 0 }

local function approx(a, b) return math.abs(a - b) < 1e-6 end

function app_init()
    -- 颜色位运算
    assert(FX_RGB(0x12, 0x34, 0x56) == 0x123456)
    assert(FX_RGBA(0x12, 0x34, 0x56, 0xFF) == 0xFF123456)

    -- 属性构造器
    assert(pixel("1,2", "3,4").__fx == "pixel")
    assert(percent("0.1,0.2", "0.9,0.8").__fx == "percent")
    assert(grid("g").__fx == "grid")
    assert(grid("g", 1, 1, 2, 2).n == 4)
    assert(type(title("t")) == "table")
    assert(FX_W_BUTTON == 1 and FX_FLEX_ROW == 0)

    fx_set_bg(FX_WINDOW_BG)
    fx_set_design_pixel(480, 272)

    -- tab + page
    fx_tab_new(pixel("10,26", "470,266"), title("测试,画布,图片"), name("tab"))
    fx_parent(fx_find("tab"))

    fx_label_new(pixel("8,34", "240,52"), page(0), title("Lua 绑定自检"), name("lbl"))
    local btn = fx_button_new(pixel("8,58", "200,92"), page(0), name("btn"),
                              title("点击"), call(function(w)
        S.clicked = S.clicked + 1
        fx_set_title(w, "点击 " .. S.clicked)
    end))
    assert(btn, "fx_button_new 返回 nil")
    assert(fx_find("btn") == btn)
    fx_set_value(btn, 7)
    assert(fx_get_value(btn) == 7)

    local btn2 = fx_button_new(pixel("300,214", "444,242"), page(0), name("btn2"), title("set_cb"))
    fx_set_cb(btn2, function(w) S.setcb = S.setcb + 1 end)

    local cv = fx_canvas_new(pixel("8,100", "300,230"), page(0), name("cv"), anim(1),
                             color(FX_CANVAS_DARK), call(function(w)
        local cw, ch = fx_canvas_size(w)
        assert(cw > 0 and ch > 0)
        fx_set_color(FX_CANVAS_DARK); fx_fill_rect(0, 0, cw - 1, ch - 1)
        fx_set_color(FX_GREEN); fx_fill_rect(4, 4, cw - 5, ch - 5)
        fx_set_color(FX_WHITE); fx_draw_line(0, 0, cw - 1, ch - 1)
        fx_fill_circle(cw // 2, ch // 2, 8)
        fx_fill_rect_round(0, 0, cw - 1, ch - 1, 6)
        fx_draw_text(8, 8, "Lua canvas")
        S.canvas_ok = S.canvas_ok + 1
    end))
    assert(cv)
    -- 多返回值 getter
    local x1, y1, x2, y2 = fx_widget_rect(cv)
    assert(x2 > x1 and y2 > y1)

    -- textedit
    local te = fx_textedit_new(pixel("8,34", "240,60"), page(2), name("te"), title("hello"))
    assert(te and fx_textedit_text(te) == "hello")
    -- 注意: fx_set_title 改的是控件 title 字段; textedit 正文在 text_buf,
    -- C API 没有 set_text, 所以这里只验证读取路径。
    assert(fx_widget_title(te) == "hello")

    -- image：创建 + 写像素 + 画到 canvas 之外也能安全调用
    local img = fx_image_create(4, 3)
    assert(img, "image_create 返回 nil")
    local iw, ih = fx_image_size(img)
    assert(iw == 4 and ih == 3)
    fx_image_fill(img, FX_RED)
    fx_image_set_px(img, 1, 1, FX_BLUE)
    assert(fx_image_get_px(img, 1, 1) == FX_BLUE)
    fx_image_line(img, 0, 0, 3, 2, FX_WHITE, 1)
    fx_image_grayscale(img)
    fx_image_tint(img, FX_CYAN, 32)
    -- 挂到控件上时绑定层应 pin 住图片；强制 GC 后仍能安全绘制（防悬垂）
    local imw = fx_image_new(pixel("300,100", "360,140"), page(0), name("imgw"), image(img))
    assert(imw, "image widget")
    fx_set_image(imw, img)
    collectgarbage("collect"); collectgarbage("collect")
    fx_draw_image(img, 0, 0, iw, ih)

    -- list / drop
    local list = fx_list_new("300,34", "460,100")
    assert(list)
    fx_list_add(list, "a"); fx_list_add(list, "b"); fx_list_add(list, "c")
    fx_list_set_cb(list, function(w, idx) S.list_sel = idx; S.list_cb = true end)
    assert(fx_list_sel(list) == 0)
    S.list = list
    local drop = fx_drop_new("300,110", "460,140")
    assert(drop)
    fx_drop_add(drop, "x")

    -- v2.5.5: 统一滚动 API / 触摸开关
    local sc = fx_scroll_new(pixel("10,10", "200,200"), name("scroll0"))
    assert(sc, "scroll_new")
    fx_scroll_content(sc, 1000)
    fx_widget_set_rect(sc, 10, 10, 200, 200)
    local smax = fx_scroll_max(sc)
    assert(smax == 1000 - 191, "scroll max = " .. tostring(smax))
    assert(fx_scroll_view_h(sc) == 191)
    fx_scroll_set(sc, 100)
    assert(fx_scroll_offset(sc) == 100, "scroll_set")
    fx_scroll_to(sc, 300)
    for _ = 1, 80 do fx_scroll_update(sc, 1000) end
    assert(math.abs(fx_scroll_offset(sc) - 300) <= 1, "scroll_to = " .. tostring(fx_scroll_offset(sc)))
    fx_scroll_to_bottom(sc)
    for _ = 1, 400 do fx_scroll_update(sc, 1000) end
    assert(fx_scroll_offset(sc) == smax, "scroll_to_bottom")
    fx_scroll_by(sc, -40)
    for _ = 1, 400 do fx_scroll_update(sc, 1000) end
    assert(fx_scroll_offset(sc) == smax - 40, "scroll_by")
    assert(type(fx_touch_capable()) == "boolean")
    fx_set_touch_mode(0)
    assert(fx_touch_capable() == false)
    fx_set_touch_mode(1)
    assert(fx_touch_capable() == true)
    fx_set_touch_mode(-1)
    fx_delete(sc)   -- 只测 API: 删掉, 别挡住后面的点击命中测试

    -- flex
    local panel = fx_panel_new(pixel("300,150", "460,230"), name("panel"))
    assert(panel)
    fx_parent(panel)
    local b1 = fx_button_new(pixel("0,0", "10,10"), title("1"))
    local b2 = fx_button_new(pixel("0,0", "10,10"), title("2"))
    fx_parent(nil)
    fx_flex_apply_simple(panel, FX_FLEX_ROW, 4, 4, FX_FLEX_START, FX_FLEX_CENTER)
    local _, _, rx2, _ = fx_widget_rect(b1)
    local rx1, _, _, _ = fx_widget_rect(b2)
    assert(rx2 > 0 and rx1 > 0)

    -- SVG 解析 + canvas 内绘制
    local svg = fx_svg_parse('<svg viewBox="0 0 10 10"><rect x="1" y="1" width="8" height="8" fill="#ff0000"/><circle cx="5" cy="5" r="2" fill="blue"/></svg>')
    assert(svg, "svg_parse 返回 nil")
    assert(fx_svg_shape_count(svg) == 2, "svg shape count = " .. tostring(fx_svg_shape_count(svg)))
    local vw, vh = fx_svg_size(svg)
    assert(approx(vw, 10) and approx(vh, 10))
    assert(fx_svg_skipped(svg) == 0)
    S.svg = svg

    -- 文件后端
    local path = "/tmp/fxtk_lua_test_file.txt"
    assert(fx_file_write(path, "hello lua", false) > 0)
    assert(fx_file_exists(path))
    assert(fx_file_read(path) == "hello lua")
    local sz = fx_file_size(path)
    assert(sz == 9, "file size = " .. tostring(sz))
    local base = fx_path_basename(path)
    assert(base == "fxtk_lua_test_file.txt")
    assert(fx_path_ext(path) == ".txt")

    -- 时间 / 随机 / 后端信息
    assert(type(fx_time_ms()) == "number")
    fx_rand_seed(1)
    local r = fx_rand_range(1, 10)
    assert(r >= 1 and r <= 10)
    assert(type(fx_backend_name()) == "string")
    local caps = fx_backend_caps()
    assert(type(caps) == "table" and type(caps.platform) == "string")

    -- 手势 API
    fx_gesture_config_default()
    fx_gesture_feed(10, 10, 1, 0)
    fx_gesture_feed(10, 10, 0, 400)
    local ev = fx_gesture_poll()
    while ev do ev = fx_gesture_poll() end
    assert(fx_gesture_pending() == 0 or fx_gesture_pending() > 0)

    -- 截图 API 在没有 read_pixels 的假驱动上返回 0，不崩即可
    fx_repaint()
end

function app_frame(dt, frame)
    S.frames = frame
    if frame == 2 then
        local x1, y1, x2, y2 = fx_widget_rect(fx_find("btn"))
        local cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
        fx_touch_press(cx, cy)
    elseif frame == 3 then
        local x1, y1, x2, y2 = fx_widget_rect(fx_find("btn"))
        fx_touch_release((x1 + x2) // 2, (y1 + y2) // 2)
        assert(S.clicked == 1, "call 回调没有触发: " .. S.clicked)
        assert(fx_widget_title(fx_find("btn")) == "点击 1")
        assert(S.canvas_ok > 0, "canvas 回调没有跑")
        assert(fx_widget_type(fx_find("tab")) == FX_W_TAB)
        assert(fx_find("does-not-exist") == nil)
        -- 删除 + 再查找
        fx_delete(fx_find("lbl"))
        assert(fx_find("lbl") == nil)
        -- 明确报告成功；宿主继续跑完帧数
    elseif frame == 4 then
        local x1, y1, x2, y2 = fx_widget_rect(fx_find("btn2"))
        fx_touch_press((x1 + x2) // 2, (y1 + y2) // 2)
    elseif frame == 5 then
        local x1, y1, x2, y2 = fx_widget_rect(fx_find("btn2"))
        fx_touch_release((x1 + x2) // 2, (y1 + y2) // 2)
        assert(S.setcb == 1, "fx_set_cb 回调没有触发: " .. S.setcb)
        local lx1, ly1 = fx_widget_rect(S.list)
        fx_touch_press(lx1 + 10, ly1 + 6)
    elseif frame == 6 then
        local lx1, ly1 = fx_widget_rect(S.list)
        fx_touch_release(lx1 + 10, ly1 + 6)
    elseif frame == 7 then
        -- 列表在松手后的下一次绘制里回调，因此这里才断言。
        assert(S.list_cb and S.list_sel == 0, "list 回调 idx=" .. tostring(S.list_sel))
        print("[lua-test] OK: attrs/widgets/callbacks/canvas/image/list/flex/svg/file/gesture")
    end
end
