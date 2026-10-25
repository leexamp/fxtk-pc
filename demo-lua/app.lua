--[[
app.lua — fxtk 综合演示的 Lua 重写版

C 核心不变，界面全部用 Lua 写；写法与 C 的 fxtk.h 属性宏保持一致：

    fx_label_new(pixel("16,12", "464,40"), title("设置"))
    fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"), call(on_ok))

页签：波形 / 图形 / 控件 / 图片 / 3D / 输入 / 画板 / 键鼠 / 压测 / 滚动 / 组件 / 粒子
运行：
    ./fxtk_lua_runner              # sokol 窗口（脚本运行时加载；./fxtk_lua 是兼容名）
    make lua-test                  # 无头自检
    ./test/lua_host_sdl demo-lua/app.lua 60 3 /tmp/p3.png   # 无头截图
]]
require("fxtk").install_globals()

-- ==================== 小工具 ====================

local PI = math.pi
local sin, cos, floor, abs, min, max = math.sin, math.cos, math.floor, math.abs, math.min, math.max

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function rect_of(w)
    return fx_widget_rect(w)
end

local function now_ms()
    return fx_time_ms()
end

-- ==================== 全局状态 ====================

local S = {
    -- 页0 波形
    phase = 0, wave_on = true, speed = 30,
    -- 页1 图形
    gfx_t = 0, spin = 30, gfx_mode = 0,
    -- 页2 控件
    clicks = { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    -- 页3 图片
    pic = 0, pics = {}, imported = nil,
    quad_n = { 0.04, 0.04, 0.96, 0.04, 0.96, 0.96, 0.04, 0.96 },
    quad_scale = 1.0, quad_drag = -1, quad_show = true,
    -- 页4 3D
    oc = false, gpu = true, rt_time = 0, rspeed = 30, rqual = 50,
    fps_frames = 0, fps_t0 = 0,
    -- 页7 键鼠
    key_log = {}, key_log_n = 0, key_prev = nil, prev_mx = -9999, prev_my = -9999,
    prev_mp = -1, key_have = false, key_frm = 0,
    -- 页6 画板
    pbuf = nil, brush = FX_RED, px = -1, py = -1,
    -- 页8 压测
    mv_clicks = 0, mv_extra = 0, mv_dyn = {}, mv_t = 0,
    -- 页10 组件
    names = { "张伟", "王芳", "李娜", "刘洋", "陈静", "杨帆", "赵磊", "黄敏", "周涛", "吴婷", "Alice", "Bob" },
    nclist = nil,
    fs_dir = "", fs_ent = {}, fs_sel = -1, fs_hov = -1,
    dbx = { 20, 150, 90 }, dby = { 20, 20, 44 }, drag_i = -1,
    -- 页11 粒子
    pt_img = nil, pt_img_w = 0, pt_img_h = 0, pt_n = 600, pt_last = 0, pt_fps = 0,
    pt_list = {},
    -- 页1 伪 3D 贴图
    p3_floor = nil, p3_wall = nil, p3_spr = nil, p3_n = 0, p3_t = 0, p3_cube = 0,
    -- 页7 十字标
    trail_x = {}, trail_y = {}, trail_i = 0,
}

-- ==================== 页0：波形画布 ====================

local function on_canvas(w)
    local cw, ch = fx_canvas_size(w)
    fx_set_color(0x080808); fx_fill_rect(0, 0, cw - 1, ch - 1)
    fx_set_color(0x101410)
    for x = 0, cw - 1, 16 do fx_draw_vline(x, 0, ch - 1) end
    for y = 0, ch - 1, 16 do fx_draw_hline(0, cw - 1, y) end
    if S.wave_on then
        local amp = ch // 2 - 8
        local prev_y = ch // 2
        local pi2_32 = PI / 32.0
        fx_set_color(FX_YELLOW)
        for x = 0, cw - 1 do
            local t = (x + S.phase) * pi2_32
            local y = ch // 2 + floor(amp * sin(t) * 0.7 + amp * 0.3 * sin(t / 3.0))
            fx_draw_line(x > 0 and x - 1 or 0, prev_y, x, y)
            prev_y = y
        end
        fx_set_color(FX_RED)
        fx_fill_circle(cw // 2, ch // 2 + floor(amp * sin((cw // 2 + S.phase) * pi2_32) * 0.7), 6)
    end
    S.phase = (S.phase + S.speed) % 4096
end

local function on_speed(w)
    S.speed = fx_get_value(w)
    fx_set_value(fx_find("speed_bar"), S.speed)
end

local function on_wave(w)
    S.wave_on = fx_get_value(w) ~= 0
end

-- ==================== 页1：图形 / 伪 3D ====================

local HEX_PTS = { 40, 0, 20, 29, -20, 29, -40, 0, -20, -29, 20, -29 }
local TRAIL_N = 14

local function p3_tiles()
    if S.p3_floor then return end
    S.p3_floor = fx_image_create(64, 64)
    S.p3_wall  = fx_image_create(64, 64)
    S.p3_spr   = fx_image_create(32, 32)
    if not S.p3_floor then return end
    for y = 0, 63 do
        for x = 0, 63 do
            local c = (((x >> 3) + (y >> 3)) & 1)
            fx_image_set_px(S.p3_floor, x, y, c ~= 0 and FX_RGB(206, 210, 218) or FX_RGB(116, 122, 132))
            local b = (((x >> 4) & 1) ~ ((y >> 2) & 1))
            fx_image_set_px(S.p3_wall, x, y, b ~= 0 and FX_RGB(158, 96, 74) or FX_RGB(122, 68, 54))
        end
    end
    for y = 0, 31 do
        for x = 0, 31 do
            local dx, dy = x - 16, y - 16
            fx_image_set_px(S.p3_spr, x, y, (dx * dx + dy * dy) < 210 and FX_RGB(255, 214, 64) or FX_RGB(24, 40, 78))
        end
    end
end

local function p3_proj(wx, wy, wz, a, cw, ch)
    local c, s = cos(a), sin(a)
    local X = wx * c - wz * s
    local Z = wx * s + wz * c
    local Y = wy - 1.45
    if Z < 0.35 then return nil end
    local f = cw * 1.05
    return cw * 0.5 + f * X / Z, ch * 0.60 - f * Y / Z, Z
end

local function p3_face(tx, a, cw, ch, wx, wy, wz)
    local q = {}
    for i = 1, 4 do
        local sx, sy = p3_proj(wx[i], wy[i], wz[i], a, cw, ch)
        if not sx then return end
        q[i * 2 - 1] = sx
        q[i * 2] = sy
    end
    fx_draw_image_quad(tx, q)
    S.p3_n = S.p3_n + 1
end

local function p3_scene(cw, ch)
    p3_tiles()
    if not S.p3_floor then return end
    S.p3_n = 0
    local a = 0.38 * sin(S.p3_t * 0.45)
    fx_set_color(FX_RGB(8, 10, 18)); fx_fill_rect(0, 0, cw - 1, ch - 1)

    for iz = 0, 7 do
        for ix = -2, 2 do
            local x0 = ix * 1.2
            local x1 = x0 + 1.2
            local z0 = 1.6 + iz * 1.1
            local z1 = z0 + 1.1
            local wx = { x0, x1, x1, x0 }
            local wz = { z0, z0, z1, z1 }
            local wy = { 0, 0, 0, 0 }
            p3_face(S.p3_floor, a, cw, ch, wx, wy, wz)
            wy[1], wy[2], wy[3], wy[4] = 2.9, 2.9, 2.9, 2.9
            p3_face(S.p3_floor, a, cw, ch, wx, wy, wz)
        end
    end

    for iz = 0, 7 do
        local z0 = 1.6 + iz * 1.1
        local z1 = z0 + 1.1
        for side = 0, 1 do
            local x = side ~= 0 and 2.6 or -2.6
            local wx = { x, x, x, x }
            local wz = { z0, z0, z1, z1 }
            local wy = { 0.0, 2.9, 2.9, 0.0 }
            p3_face(S.p3_wall, a, cw, ch, wx, wy, wz)
        end
    end

    do
        local FI = { {1,2,3,4}, {6,5,8,7}, {5,1,4,8}, {2,6,7,3}, {5,6,2,1}, {4,3,7,8} }
        local V = {}
        local ca, sa = cos(S.p3_cube), sin(S.p3_cube)
        local cb, sb = cos(S.p3_cube * 0.7), sin(S.p3_cube * 0.7)
        for i = 0, 7 do
            local x = (i & 1) ~= 0 and 0.42 or -0.42
            local y = (i & 2) ~= 0 and 0.42 or -0.42
            local z = (i & 4) ~= 0 and 0.42 or -0.42
            local x1 = x * ca - z * sa
            local z1 = x * sa + z * ca
            local y1 = y * cb - z1 * sb
            local z2 = y * sb + z1 * cb
            V[i + 1] = { x1, y1 + 1.45, z2 + 4.2 }
        end
        local order, fz = {}, {}
        for i = 1, 6 do
            order[i] = i; fz[i] = 0
            for k = 1, 4 do fz[i] = fz[i] + V[FI[i][k]][3] end
        end
        for i = 1, 5 do
            for j = i + 1, 6 do
                if fz[order[j]] > fz[order[i]] then
                    order[i], order[j] = order[j], order[i]
                end
            end
        end
        for i = 1, 6 do
            local fi = order[i]
            local wx, wy, wz = {}, {}, {}
            for k = 1, 4 do
                local v = V[FI[fi][k]]
                wx[k], wy[k], wz[k] = v[1], v[2], v[3]
            end
            p3_face(S.p3_floor, a, cw, ch, wx, wy, wz)
        end
    end

    for k = 0, 2 do
        local sx, sy, z = p3_proj(-1.8 + k * 1.8, 1.5, 2.6 + k * 1.6, a, cw, ch)
        if sx then
            local s = 26.0 / z * (cw / 440.0) * 3.0
            local q = { sx - s, sy - s, sx + s, sy - s, sx + s, sy + s, sx - s, sy + s }
            fx_draw_image_quad(S.p3_spr, q)
            S.p3_n = S.p3_n + 1
        end
    end

    local buf = string.format("四边形 %d · fps %d · %s", S.p3_n, fxtk_fps(),
                              fx_quad_warp_gpu() ~= 0 and "GPU 形变" or "CPU 形变")
    fx_draw_text_c(6, ch - 16, buf, FX_WHITE, FX_RGB(8, 10, 18))
    S.p3_t = S.p3_t + 1.0
    S.p3_cube = S.p3_cube + 0.020
end

local function on_gfx(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    if S.gfx_mode == 3 then p3_scene(cw, ch); return end
    local ks = cw / 440.0
    if ks < 0.5 then ks = 0.5 end
    local ang = S.gfx_t % 360
    fx_set_color(0x000083); fx_fill_rect(0, 0, cw - 1, ch - 1)
    if S.gfx_mode ~= 2 and S.pics[1] then
        fx_draw_image_rot(S.pics[1], cw // 4, ch // 2, ang, floor(90 * ks))
        fx_draw_image_rot(S.pics[3], cw * 3 // 4, ch // 2, -ang, floor(70 * ks))
    end
    if S.gfx_mode ~= 1 then
        local hex = {}
        for k = 1, 12 do hex[k] = floor(HEX_PTS[k] * ks) end
        fx_set_color(FX_MAGENTA)
        fx_fill_polygon_rot(hex, cw // 2, ch // 2, ang)
        local T = floor(26 * ks)
        local tri = { 0, -T, floor(T * 0.85), T // 2, -floor(T * 0.85), T // 2 }
        fx_set_color(FX_YELLOW)
        fx_fill_polygon_rot(tri, cw // 2, ch // 2, -ang * 2)
        local bx = cw // 2 + floor((cw // 2 - 50 * ks) * sin(S.gfx_t * 0.021))
        local by = ch // 2 + floor((ch // 2 - 40 * ks) * cos(S.gfx_t * 0.034))
        fx_set_color(0x838183)
        for k = 0, TRAIL_N - 1 do
            local idx = (S.trail_i - 1 - k + TRAIL_N * 4) % TRAIL_N + 1
            local r = floor((5 - k / 3) * ks)
            if r > 0 and S.trail_x[idx] then fx_fill_circle(S.trail_x[idx], S.trail_y[idx], r) end
        end
        S.trail_i = S.trail_i % TRAIL_N + 1
        S.trail_x[S.trail_i], S.trail_y[S.trail_i] = bx, by
        fx_set_color(FX_WHITE); fx_draw_circle(bx, by, floor(8 * ks))
        fx_set_color(FX_RED);   fx_fill_circle(bx, by, floor(6 * ks))
    end
    fx_set_color(FX_CYAN)
    fx_fill_rect_round(cw - floor(70 * ks), floor(8 * ks), cw - floor(10 * ks), floor(44 * ks), floor(8 * ks))
    fx_set_color(FX_WHITE)
    fx_draw_arc(cw - floor(40 * ks), ch - floor(40 * ks), floor(24 * ks), 0, 270)
    S.gfx_t = S.gfx_t + 1 + S.spin // 10
end

local function on_mode(w)
    S.gfx_mode = (S.gfx_mode + 1) % 4
    local m = { "模式: 全部", "模式: 贴图", "模式: 矢量", "模式: 伪3D(四边形形变)" }
    fx_set_title(fx_find("gfx_info"), m[S.gfx_mode + 1])
end

local function on_spin(w)
    S.spin = fx_get_value(w)
end

-- ==================== 页2：控件 ====================

local KEYS = { "1", "2", "3", "4", "5", "6", "7", "8", "9" }

local function on_key(w, id)
    S.clicks[id + 1] = S.clicks[id + 1] + 1
    fx_set_title(fx_find("info"), string.format("第 %d 键 · 累计 %d 次", id + 1, S.clicks[id + 1]))
end

local function cp_num(s)
    if not s or s == "" then return 0 end
    local v = tonumber(s) or 0
    return clamp(floor(v), 0, 255)
end

local function cp_apply()
    local b = fx_find("cp_btn")
    if b then
        fx_set_color_w(b, FX_RGB(S.cr or 255, S.cg or 90, S.cb or 0))
        fx_set_fgcolor_w(b, FX_RGB(S.tr or 40, S.tg or 40, S.tb or 40))
    end
    local sw = fx_find("cp_sw")
    if sw then fx_set_color_w(sw, FX_RGB(S.cr or 255, S.cg or 90, S.cb or 0)) end
end

local function cp_cb()
    local function get(n) return cp_num(fx_textedit_text(fx_find(n))) end
    S.cr, S.cg, S.cb = get("cp_br"), get("cp_bg"), get("cp_bb")
    S.tr, S.tg, S.tb = get("cp_tr"), get("cp_tg"), get("cp_tb")
    cp_apply()
end

local function on_reset()
    for i = 1, 9 do S.clicks[i] = 0 end
    S.phase = 0
    fx_set_title(fx_find("info"), "已重置 · 点击数字键试试")
end

-- ==================== 页3：图片 + 四边形形变 ====================

local function img_info_set(fmt, ...)
    local w = fx_find("img_info")
    if w then fx_set_title(w, string.format(fmt, ...)) end
end

local function quad_corner_px(cw, ch, i)
    local n = S.quad_n
    local ox = (0.5 + (n[i * 2 + 1] - 0.5) * S.quad_scale) * cw
    local oy = (0.5 + (n[i * 2 + 2] - 0.5) * S.quad_scale) * ch
    return ox, oy
end

local function on_imgq(w)
    local cw, ch = fx_canvas_size(w)
    fx_canvas_clear(w, FX_RGB(245, 245, 245))
    local img = S.imported or S.pics[S.pic + 1]
    if not img then return end
    local x1, y1 = rect_of(w)
    local mx, my, mp = fx_touch_state()
    local lx, ly = mx - x1, my - y1

    if mp ~= 0 and S.quad_drag < 0 then
        for i = 0, 3 do
            local hx, hy = quad_corner_px(cw, ch, i)
            if lx >= floor(hx) - 10 and lx <= floor(hx) + 10 and ly >= floor(hy) - 10 and ly <= floor(hy) + 10 then
                S.quad_drag = i
                break
            end
        end
    end
    if mp == 0 then S.quad_drag = -1 end
    if S.quad_drag >= 0 then
        local nx, ny = lx / cw, ly / ch
        local sc = S.quad_scale > 0.01 and S.quad_scale or 1.0
        S.quad_n[S.quad_drag * 2 + 1] = 0.5 + (nx - 0.5) / sc
        S.quad_n[S.quad_drag * 2 + 2] = 0.5 + (ny - 0.5) / sc
        for c = 1, 2 do
            local k = S.quad_drag * 2 + c
            S.quad_n[k] = clamp(S.quad_n[k], -1.5, 2.5)
        end
    end

    local q = {}
    for i = 0, 3 do
        q[i * 2 + 1], q[i * 2 + 2] = quad_corner_px(cw, ch, i)
    end
    fx_draw_image_quad(img, q)
    fx_draw_text_c(8, ch - 20, "拖动四角顶点 → 任意四边形", FX_RGB(90, 90, 90), FX_RGB(245, 245, 245))
    if S.quad_show then
        fx_set_color(FX_RGB(200, 60, 60))
        for i = 0, 3 do
            local j = (i + 1) % 4
            fx_draw_line(floor(q[i * 2 + 1]), floor(q[i * 2 + 2]), floor(q[j * 2 + 1]), floor(q[j * 2 + 2]))
        end
        for i = 0, 3 do
            local hx, hy = floor(q[i * 2 + 1]), floor(q[i * 2 + 2])
            local near_h = abs(lx - hx) <= 14 and abs(ly - hy) <= 14
            fx_set_color((S.quad_drag == i or near_h) and FX_RGB(255, 160, 0) or FX_WHITE)
            fx_fill_rect(hx - 7, hy - 7, hx + 7, hy + 7)
            fx_set_color(FX_RGB(160, 40, 40))
            fx_draw_rect(hx - 8, hy - 8, hx + 8, hy + 8)
            fx_draw_rect(hx - 7, hy - 7, hx + 7, hy + 7)
        end
    end
end

local function on_img(w)
    S.pic = (S.pic + 1) % 3
    S.imported = nil
    S.quad_n = { 0.04, 0.04, 0.96, 0.04, 0.96, 0.96, 0.04, 0.96 }
    img_info_set("内置图案 %d / 3", S.pic + 1)
end

local function on_img_load(w)
    local path = os.getenv("FXTK_IMPORT")
    if not path or path == "" then
        path = fx_backend_pick_file("选择图片", "png,jpg,jpeg,bmp,gif,tga")
    end
    if not path then
        img_info_set("已取消 / 对话框不可用 (可设 FXTK_IMPORT=<路径> 直接导入)")
        return
    end
    local img = fx_image_load(path)
    if not img then img_info_set("解码失败: %s", fx_path_basename(path)); return end
    S.imported = img
    S.quad_scale = 1.0
    local iw, ih = fx_image_size(img)
    img_info_set("已导入 %s (%dx%d)", fx_path_basename(path), iw, ih)
end

local function on_img_reset(w)
    S.quad_n = { 0.04, 0.04, 0.96, 0.04, 0.96, 0.96, 0.04, 0.96 }
    S.quad_scale = 1.0
    S.quad_show = true
    img_info_set("四边形已复位")
end

local function on_zoom(w)
    S.quad_scale = 0.4 + fx_get_value(w) / 100.0 * 1.6
end

-- ==================== 页4：3D 光线步进 ====================

local function on_rspeed(w) S.rspeed = fx_get_value(w) end
local function on_rqual(w)  S.rqual  = fx_get_value(w) end

local function on_oc(w)
    S.oc = not S.oc
    fx_set_title(w, S.oc and "超频: 开" or "超频: 关")
end

local function on_gpu(w)
    S.gpu = not S.gpu
    fx_set_title(w, S.gpu and "GPU: 开" or "GPU: 关")
    fx_repaint()
end

local function on_3d(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    local gpu_direct = S.gpu and fx_raymarch_available() ~= 0
    if gpu_direct then
        fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1)
        fx_draw_raymarch(S.rt_time, 0, 0, cw, ch)
    else
        -- 这个宿主没有 GPU raymarch 钩子（SDL/无头）：用 Lua 画一个可动的替代场景，
        -- 保证页面不空白；真正的 CPU raymarch C 实现在 demo-main 里，Lua 版不重复移植。
        fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1)
        local cx, cy = cw // 2, ch // 2
        for k = 1, 8 do
            local r = (k * 18 + floor(S.rt_time * 20)) % (min(cw, ch) // 2)
            fx_set_color(FX_RGB(20 + k * 20, 60 + k * 12, 140 - k * 8))
            fx_draw_circle(cx, cy, r)
        end
        fx_set_color(FX_GREEN)
        fx_draw_text_c(8, ch - 22, "3D 页: 当前宿主无 GPU raymarch，使用 Lua 替代动画", FX_GREEN, FX_BLACK)
    end
    S.rt_time = S.rt_time + 0.016 * (0.3 + S.rspeed * 0.06)

    S.fps_frames = S.fps_frames + 1
    local now = now_ms()
    if S.fps_t0 == 0 then S.fps_t0 = now end
    if now - S.fps_t0 >= 500 then
        local fps = floor(S.fps_frames * 1000 / (now - S.fps_t0))
        fx_set_title(fx_find("fps_lbl"), string.format("FPS: %d (%dx%d) [%s]", fps, cw, ch,
            gpu_direct and "GPU 着色器" or "Lua 回退"))
        S.fps_frames = 0
        S.fps_t0 = now
    end
end

-- ==================== 页6：画板 ====================

local function on_clear()
    if S.pbuf then fx_image_fill(S.pbuf, FX_WHITE) end
    S.px, S.py = -1, -1
end

local function on_paint(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    local PW, PH = 220, 120
    if not S.pbuf then
        S.pbuf = fx_image_create(PW, PH)
        if S.pbuf then on_clear() end
    end
    if not S.pbuf then return end
    local mx, my, mp = fx_touch_state()
    if mp ~= 0 and fx_pressed() == w then
        local px = (mx - x1) * PW // cw
        local py = (my - y1) * PH // ch
        if S.px >= 0 then
            fx_image_line(S.pbuf, S.px, S.py, px, py, S.brush, 2)
        else
            fx_image_line(S.pbuf, px, py, px, py, S.brush, 2)
        end
        S.px, S.py = px, py
    else
        S.px, S.py = -1, -1
    end
    fx_draw_image(S.pbuf, 0, 0, cw, ch)
end

local function on_brush(color)
    S.brush = color
end

-- ==================== 页7：键鼠监视器 ====================

local function key_label(k)
    if k.utf8 and k.utf8 ~= "" then return "按键  「" .. k.utf8 .. "」" end
    if k.key == FX_KEY_BACKSPACE then return "按键  Backspace" end
    if k.key == FX_KEY_RETURN then return "按键  Enter" end
    if k.key == FX_KEY_ESCAPE then return "按键  Esc" end
    if k.key == FX_KEY_LEFT then return "按键  ←" end
    if k.key == FX_KEY_RIGHT then return "按键  →" end
    return string.format("按键  code=%d", k.key)
end

local function on_keys_view(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    local BG, CARD, LINE = FX_RGB(30, 30, 36), FX_RGB(42, 43, 52), FX_RGB(58, 60, 72)
    fx_set_color(BG); fx_fill_rect(0, 0, cw - 1, ch - 1)

    fx_set_color(FX_RGB(33, 150, 243))
    fx_fill_rect_round(10, 10, cw - 11, 34, 6)
    fx_draw_text_c(20, 14, "输入监视器 · 键盘 / 鼠标 / 滚轮", FX_WHITE, FX_RGB(33, 150, 243))

    local k = fx_last_key()
    local mx, my, mp = fx_touch_state()

    S.key_frm = S.key_frm + 1
    local ev, dirty = nil, false
    if S.key_have and (k.key ~= S.key_prev.key or k.utf8 ~= S.key_prev.utf8) then
        ev, dirty = key_label(k), true
    elseif mp ~= S.prev_mp then
        ev, dirty = "鼠标  " .. (mp ~= 0 and "按下" or "松开"), true
    elseif mp ~= 0 and (mx ~= S.prev_mx or my ~= S.prev_my) then
        ev, dirty = string.format("拖动  移到 (%d, %d)", mx, my), true
    end
    if dirty then
        S.key_log_n = S.key_log_n % 18 + 1
        S.key_log[S.key_log_n] = string.format("t=%-5d %s", S.key_frm, ev)
    end
    S.key_prev = k; S.prev_mx, S.prev_my, S.prev_mp = mx, my, mp; S.key_have = true

    local cardw = (cw - 34) // 2
    local ttl = { "键盘", "鼠标" }
    local val = {}
    if k.utf8 and k.utf8 ~= "" then val[1] = "字符 " .. k.utf8
    elseif k.key ~= 0 then val[1] = "键码 " .. k.key
    else val[1] = "等待输入…" end
    val[2] = string.format("%d, %d  ·  %s", mx, my, mp ~= 0 and "按下" or "松开")
    for i = 1, 2 do
        local bx = 10 + (i - 1) * (cardw + 14)
        fx_set_color(CARD); fx_fill_rect_round(bx, 44, bx + cardw, 90, 6)
        fx_set_color(LINE);  fx_draw_rect_round(bx, 44, bx + cardw, 90, 6)
        fx_draw_text_c(bx + 10, 50, ttl[i], FX_RGB(140, 145, 160), CARD)
        fxtk_draw_text_size(14, bx + 10, 68, val[i], FX_WHITE, CARD)
    end

    local ly0, lh = 102, 18
    local rows = clamp((ch - ly0 - 34) // lh, 1, 18)
    fx_set_color(CARD); fx_fill_rect_round(10, ly0 - 6, cw - 11, ly0 - 6 + rows * lh + 22, 6)
    fx_set_color(LINE);  fx_draw_rect_round(10, ly0 - 6, cw - 11, ly0 - 6 + rows * lh + 22, 6)
    fxtk_draw_text_size(13, 20, ly0, "事件日志(最近 18 条)", FX_RGB(140, 145, 160), CARD)
    for i = 0, rows - 1 do
        local idx = ((S.key_log_n - i - 1) % 18) + 1
        local text = S.key_log[idx]
        if text then
            local y = ly0 + 20 + i * lh
            local rowbg = (i % 2 == 1) and CARD or FX_RGB(36, 37, 45)
            fx_set_color(rowbg); fx_fill_rect(16, y, cw - 17, y + lh - 2)
            fx_draw_text_c(22, y + 1, text, i == 0 and FX_YELLOW or FX_RGB(190, 195, 210), rowbg)
        end
    end

    fxtk_draw_text_size(13, 20, ch - 22,
        "提示: 移动鼠标看画布内十字标 · 点击任意按钮看状态变化 · 滚轮滚动会记录在事件日志里",
        FX_RGB(110, 115, 130), BG)
    if mx >= x1 and mx <= x2 and my >= y1 and my <= y2 then
        local px, py = mx - x1, my - y1
        fx_set_color(mp ~= 0 and FX_GREEN or FX_RGB(120, 200, 140))
        fx_draw_hline(px - 12, px + 12, py)
        fx_draw_vline(px, py - 12, py + 12)
    end
end

-- ==================== 页8：控件移动压测 ====================

local function on_mv_click(w)
    S.mv_clicks = S.mv_clicks + 1
    fx_set_title(w, string.format("被点 %d 次", S.mv_clicks))
end

local function on_mv_extra_click(w)
    fx_set_title(w, "x")
end

local function on_move(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    fx_set_color(FX_RGB(20, 20, 26)); fx_fill_rect(0, 0, cw - 1, ch - 1)
    S.mv_t = S.mv_t + 1
    local t = S.mv_t
    fx_repaint_rect(x1, y1, x2, y2)
    local total = fxtk_widget_count()
    fx_draw_text_c(10, 8, string.format("控件移动压测 t=%d · 总控件 %d", t, total), FX_GREEN, FX_RGB(20, 20, 26))

    local cols = max((cw - 16) // 24, 1)
    local rows = max((ch - 46) // 20, 1)
    local cap = min(cols * rows, 1200)      -- Lua 版封顶：太大的数量会让 GC/闭包成本压过渲染
    if fxtk_fps() >= 30 and S.mv_extra < cap then
        fx_parent(fx_find("move_cv"))
        local pal = {
            FX_RGB(33, 150, 243), FX_RGB(0, 172, 193), FX_RGB(76, 175, 80),
            FX_RGB(255, 179, 0), FX_RGB(244, 81, 30), FX_RGB(126, 87, 194),
        }
        local row = S.mv_extra // cols
        local nb = fx_button_new(pixel("0,0", "0,0"), title("动态"), line(9),
                                 color(pal[row % 6 + 1]), call(on_mv_extra_click))
        fx_parent(fx_find("tab"))
        if nb then
            local nx = 8 + (S.mv_extra % cols) * 24
            local ny = 30 + (S.mv_extra // cols) * 20
            fx_widget_fix(nb, x1 + nx, y1 + ny)
            fx_widget_set_rect(nb, x1 + nx, y1 + ny, x1 + nx + 20, y1 + ny + 16)
            S.mv_dyn[#S.mv_dyn + 1] = nb
            S.mv_extra = S.mv_extra + 1
        end
    end
    for i = 0, S.mv_extra - 1 do
        local d = S.mv_dyn[i + 1]
        if d then
            local bx = x1 + 8 + (i % cols) * 24 + floor(14 * sin(t * 0.05 + i * 0.7))
            local by = y1 + 30 + (i // cols) * 20 + floor(10 * cos(t * 0.04 + i * 1.1))
            fx_widget_set_rect(d, bx, by, bx + 20, by + 16)
        end
    end
    fx_draw_text_c(10, 24, "FPS 富余时每帧自动 +1 按钮", FX_LGRAY, FX_RGB(20, 20, 26))

    local b  = fx_find("mv_btn")
    local l  = fx_find("mv_lbl")
    local pr = fx_find("mv_prog")
    local ck = fx_find("mv_chk")
    if b then
        local bx = x1 + 20 + floor((cw - 170) * (0.5 + 0.5 * sin(t * 0.023)))
        local by = y1 + 30 + floor((ch - 100) * (0.5 + 0.5 * cos(t * 0.031)))
        fx_set_value(b, t % 100)
        fx_widget_set_rect(b, bx, by, bx + 120, by + 36)
    end
    if l then
        local lx = x1 + 10 + floor((cw - 130) * (0.5 + 0.5 * cos(t * 0.017)))
        local ly = y1 + 24 + floor((ch - 70) * (0.5 + 0.5 * sin(t * 0.041)))
        fx_set_value(l, t % 100)
        fx_widget_set_rect(l, lx, ly, lx + 100, ly + 20)
    end
    if pr then
        local px = x1 + 10 + floor((cw - 180) * (0.5 + 0.5 * sin(t * 0.013 + 2)))
        local py = y1 + ch - 64 + floor(18 * sin(t * 0.05))
        fx_set_value(pr, t % 100)
        fx_widget_set_rect(pr, px, py, px + 160, py + 14)
    end
    if ck then
        local cx = x1 + cw - 150 + floor(30 * sin(t * 0.06))
        local cy = y1 + 30 + floor((ch - 110) * (0.5 + 0.5 * sin(t * 0.027 + 1)))
        fx_set_value(ck, (t // 40) & 1)
        fx_widget_set_rect(ck, cx, cy, cx + 110, cy + 22)
    end
end

-- ==================== 页9：画布自绘滚动 ====================

local function on_scroll_view(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    local sc = fxtk_ui_scale()
    local row_h = max(40 * sc // 100, 16)
    local fs = max(18 * sc // 100, 10)
    local total = 60 * row_h
    local off = fx_scroll_update(w, total)

    fx_set_color(FX_RGB(245, 245, 245))
    fx_fill_rect(0, 0, cw - 1, ch - 1)
    local first = off // row_h
    for i = first, 59 do
        local y = i * row_h - off
        if y > ch then break end
        local t = string.format("第 %d 行内容 (滚轮翻我)", i + 1)
        if (i & 1) ~= 0 then
            fx_set_color(FX_RGB(33, 150, 243))
            fx_fill_rect(10, y + 4, cw - 20, y + row_h - 6)
            local tw = fxtk_text_width_size(fs, t)
            fxtk_draw_text_size(fs, (cw - tw) // 2, y + 12, t, FX_WHITE, FX_RGB(33, 150, 243))
        else
            fxtk_draw_text_size(fs, 10, y + 12, t, FX_RGB(40, 40, 40), FX_RGB(245, 245, 245))
        end
    end
    fx_scrollbar_draw(w, off, total)
end

-- ==================== 页10：组件 ====================

local function names_refresh()
    if not S.nclist then return end
    fx_list_clear(S.nclist)
    for i = 1, #S.names do fx_list_add(S.nclist, S.names[i]) end
end

local function on_nc_pick(w, idx)
    local p = fx_find("nc_picked")
    local name = idx and S.names[idx + 1] or "-"
    if p then fx_set_title(p, "选中: " .. name) end
end

local function on_nc_add()
    local t = fx_find("nc_txt")
    local text = t and fx_textedit_text(t) or nil
    if not text or text == "" or #S.names >= 64 then return end
    S.names[#S.names + 1] = text
    names_refresh()
end

local function on_nc_del()
    local sel = S.nclist and fx_list_sel(S.nclist) or -1
    if sel >= 0 and sel < #S.names then
        table.remove(S.names, sel + 1)
        names_refresh()
    end
end

local function on_fs_pick()
    local d = fx_backend_pick_dir()
    if d then
        S.fs_dir = d
        S.fs_ent = fx_dir_list(d, 64) or {}
        S.fs_sel = -1
        fx_set_scroll(fx_find("fs_cv"), 0)
        fx_set_title(fx_find("folder_lbl"), d)
        fx_repaint()
    end
end

local function on_fs_view(w)
    local cw, ch = fx_canvas_size(w)
    local RH, TOP = 24, 22
    local content = #S.fs_ent * RH
    local visible = ch - TOP
    local maxsc = max(content - visible, 0)
    local off = fx_scroll_update(w, content)
    local mx, my, mp = fx_touch_state()
    local x1, y1 = rect_of(w)
    local lx, ly = mx - x1, my - y1
    local sbt_h = ch - TOP
    local thumb_h = maxsc > 0 and (sbt_h * visible // content) or sbt_h
    if thumb_h < 8 then thumb_h = 8 end
    local sb_hover = lx >= cw - 16
    local tw = sb_hover and 12 or 5
    local tx = cw - tw - 3
    local thumb_y = TOP + (maxsc > 0 and (sbt_h - thumb_h) * off // maxsc or 0)
    if mp ~= 0 and fx_pressed() == w then
        if lx >= cw - 16 then
            local pos = clamp(ly - TOP, 0, sbt_h)
            if sbt_h > thumb_h then fx_set_scroll(w, pos * maxsc // sbt_h) end
        else
            local idx = (ly - TOP + off) // RH
            if idx >= 0 and idx < #S.fs_ent then S.fs_sel = idx end
        end
    end
    S.fs_hov = -1
    if lx >= 0 and lx < cw and ly >= 0 and ly < ch then
        local idx = (ly - TOP + off) // RH
        if idx >= 0 and idx < #S.fs_ent then S.fs_hov = idx end
    end

    fx_canvas_clear(w, FX_RGB(250, 250, 250))
    for i = 0, #S.fs_ent - 1 do
        local y = TOP + i * RH - off
        if y + RH >= TOP and y <= ch - 1 then
            local e = S.fs_ent[i + 1]
            local sel, hov = (i == S.fs_sel), (i == S.fs_hov)
            local c = e.is_dir and FX_BTN_BLUE or FX_RGB(85, 85, 85)
            fx_set_color(sel and FX_BTN_BLUE or (hov and FX_RGB(210, 230, 250) or FX_RGB(242, 242, 242)))
            fx_fill_rect(2, y, cw - 2, y + RH - 2)
            if hov and not sel then
                fx_set_color(FX_BTN_BLUE); fx_draw_rect(2, y, cw - 2, y + RH - 2)
            end
            local nm = e.name
            if fx_text_width(nm) > cw // 2 - 14 then nm = nm:sub(1, max(cw // 4 - 4, 1)) .. ".." end
            local bg = sel and FX_BTN_BLUE or FX_RGB(250, 250, 250)
            fxtk_draw_text_size(15, 6, y, nm, sel and FX_WHITE or c, bg)
            fxtk_draw_text_size(15, cw // 2, y, tostring(e.size), sel and FX_WHITE or c, bg)
            fxtk_draw_text_size(15, cw * 3 // 4, y, e.date, sel and FX_WHITE or c, bg)
        end
    end
    if maxsc > 0 then
        fx_set_color(FX_RGB(224, 224, 224)); fx_fill_rect(tx, TOP, cw - 2, ch - 1)
        fx_set_color(sb_hover and FX_BTN_BLUE or FX_GRAY)
        fx_fill_rect(tx, thumb_y, tx + tw - 1, thumb_y + thumb_h - 1)
    end
    fx_set_color(FX_BTN_BLUE); fx_fill_rect(0, 0, cw - 1, 20); fx_set_color(FX_WHITE)
    fx_draw_text_c(6, 4, "Name", FX_WHITE, FX_BTN_BLUE)
    fx_draw_text_c(cw // 2, 4, "Size", FX_WHITE, FX_BTN_BLUE)
    fx_draw_text_c(cw * 3 // 4, 4, "Date", FX_WHITE, FX_BTN_BLUE)
    fx_set_color(FX_GRAY); fx_draw_vline(cw // 2, 22, ch - 1); fx_draw_vline(cw * 3 // 4, 22, ch - 1)
    fx_draw_rect(0, 0, cw - 1, ch - 1)
    if S.fs_hov >= 0 and S.fs_hov < #S.fs_ent then
        local e = S.fs_ent[S.fs_hov + 1]
        local tip = string.format("%s  |  %d B  |  %s", e.name, e.size, e.date)
        local twd = min(fx_text_width(tip) + 12, cw - 4)
        local tX, tY = lx + 10, ly + 12
        if tX + twd > cw then tX = cw - twd - 2 end
        tX = max(tX, 2)
        if tY + 19 > ch then tY = ch - 19 end
        tY = max(tY, 2)
        fx_set_color(FX_RGB(40, 40, 40)); fx_fill_rect(tX, tY, tX + twd - 1, tY + 19)
        fx_set_color(FX_WHITE); fx_draw_text_c(tX + 5, tY + 3, tip, FX_WHITE, FX_RGB(40, 40, 40))
    end
end

local function on_drag(w)
    local mx, my, mp = fx_touch_state()
    local x1, y1 = rect_of(w)
    local lx, ly = mx - x1, my - y1
    local cw, ch = fx_canvas_size(w)
    local dbx, dby = S.dbx, S.dby
    if mp ~= 0 and fx_pressed() == w then
        if S.drag_i < 0 then
            local bw, bh = { 70, 58, 60 }, { 26, 46, 26 }
            for i = 1, 3 do
                if lx > dbx[i] and lx < dbx[i] + bw[i] and ly > dby[i] and ly < dby[i] + bh[i] then
                    S.drag_i = i
                    break
                end
            end
        end
        if S.drag_i >= 0 then
            dbx[S.drag_i] = lx - 35
            dby[S.drag_i] = ly - 13
        end
    else
        S.drag_i = -1
    end
    local bw, bh = { 70, 58, 60 }, { 26, 46, 26 }
    for i = 1, 3 do
        dbx[i] = clamp(dbx[i], 0, max(cw - bw[i], 0))
        dby[i] = clamp(dby[i], 0, max(ch - bh[i], 0))
    end
    fx_canvas_clear(w, FX_RGB(245, 245, 245))
    fx_set_color(FX_GRAY); fx_draw_rect(0, 0, cw - 1, ch - 1)
    fx_set_color(FX_BTN_BLUE); fx_fill_rect(dbx[1], dby[1], dbx[1] + 70, dby[1] + 26)
    fx_draw_text_c(dbx[1] + 5, dby[1] + 5, "拖我", FX_WHITE, FX_BTN_BLUE)
    fx_set_color(FX_OK_GREEN); fx_fill_rect(dbx[2], dby[2], dbx[2] + 58, dby[2] + 46)
    fx_set_color(FX_PURPLE); fx_fill_rect(dbx[3], dby[3], dbx[3] + 60, dby[3] + 26)
    fx_draw_text_c(dbx[3] + 5, dby[3] + 5, "可拖动的组件", FX_WHITE, FX_PURPLE)
end

local function on_font_scale(w)
    local fs = 12 + fx_get_value(w) * 48 // 100
    local l = fx_find("font_demo")
    if l then fx_set_fontsize(l, fs) end
    fx_repaint()
end

-- ==================== 页11：粒子性能 ====================

local PT_MAX = 4000

local function pt_reserve(n, cw, ch)
    n = clamp(n, 1, PT_MAX)
    local list = S.pt_list
    while #list < n do
        local a = (math.random(0, 359)) * 0.01745
        local sp = 0.5 + math.random(0, 99) / 100.0
        list[#list + 1] = {
            x = math.random(0, max(cw - 1, 1)),
            y = math.random(0, max(ch - 1, 1)),
            vx = cos(a) * sp,
            vy = sin(a) * sp,
            c = math.random(1, 6),
        }
    end
    for i = n + 1, #list do list[i] = nil end
end

local function on_pt_slider(w)
    S.pt_n = fx_get_value(w) * 40
end

local function on_pt(w)
    local x1, y1, x2, y2 = rect_of(w)
    local cw, ch = x2 - x1 + 1, y2 - y1 + 1
    local hw, hh = max(cw // 2, 1), max(ch // 2, 1)
    if not S.pt_img or S.pt_img_w ~= hw or S.pt_img_h ~= hh then
        S.pt_img = fx_image_create(hw, hh)
        S.pt_img_w, S.pt_img_h = hw, hh
        math.randomseed(12345)
        S.pt_list = {}
    end
    if not S.pt_img then return end
    pt_reserve(S.pt_n, cw, ch)

    local now = now_ms()
    local dt = now - (S.pt_last > 0 and S.pt_last or now)
    S.pt_last = now
    dt = clamp(dt, 0.1, 50.0)
    S.pt_fps = S.pt_fps * 0.9 + (1000.0 / dt) * 0.1

    fx_image_fill(S.pt_img, 0)
    local cols = { FX_RED, FX_GREEN, FX_BLUE, FX_YELLOW, FX_CYAN, FX_MAGENTA }
    for i = 1, #S.pt_list do
        local p = S.pt_list[i]
        p.x = p.x + p.vx * dt * 0.06
        p.y = p.y + p.vy * dt * 0.06
        if p.x < 0 then p.x = p.x + cw elseif p.x >= cw then p.x = p.x - cw end
        if p.y < 0 then p.y = p.y + ch elseif p.y >= ch then p.y = p.y - ch end
        local X, Y = floor(p.x) // 2, floor(p.y) // 2
        if X >= 0 and X < hw and Y >= 0 and Y < hh then
            fx_image_set_px(S.pt_img, X, Y, cols[p.c])
        end
    end
    fx_draw_image(S.pt_img, 0, 0, cw, ch)
    fx_draw_text_c(4, 4, string.format("FPS:%.0f  N:%d  Lua-setpx", S.pt_fps, #S.pt_list), FX_GREEN, FX_BLACK)
end

-- ==================== 构建界面 ====================

local function make_pic(kind)
    local im = fx_image_create(120, 90)
    if not im then return nil end
    for y = 0, 89 do
        for x = 0, 119 do
            local c
            if kind == 0 then
                c = FX_RGB(x * 255 // 119, y * 255 // 89, 140)
            elseif kind == 1 then
                c = (((x // 10 + y // 10) & 1) ~= 0) and FX_RGB(45, 45, 52) or FX_RGB(240, 240, 240)
            else
                local dx, dy = x - 60, y - 45
                c = (dx * dx + dy * dy < 900) and FX_RGB(244, 67, 54) or FX_RGB(33, 150, 243)
            end
            fx_image_set_px(im, x, y, c)
        end
    end
    return im
end

local function build_ui()
    fx_label_new(percent("0.02,0.01", "0.98,0.09"), title("fxtk 演示 · 标签页"), fgcolor(FX_RGB(51, 51, 51)))
    fx_tab_new(pixel("10,26", "470,266"),
               title("波形,图形,控件,图片,3D,输入,画板,键鼠,压测,滚动,组件,粒子"),
               name("tab"), color(FX_RGB(224, 224, 224)))
    fx_parent(fx_find("tab"))

    -- 页0 波形
    fx_canvas_new(pixel("6,32", "444,188"), name("wave_cv"), page(0), anim(1),
                  color(FX_RGB(30, 30, 30)), call(on_canvas))
    fx_label_new(pixel("6,194", "56,212"), page(0), title("速度"), fgcolor(FX_RGB(51, 51, 51)))
    fx_slider_new(pixel("60,194", "292,212"), name("speed"), page(0), value(S.speed),
                  color(FX_RGB(76, 175, 80)), call(on_speed))
    fx_progress_new(pixel("300,196", "444,210"), name("speed_bar"), page(0), value(S.speed))
    fx_checkbox_new(pixel("6,218", "150,240"), name("wave"), title("波形开关"), page(0),
                    value(1), fgcolor(FX_RGB(51, 51, 51)), call(on_wave))

    -- 页1 图形（canvas 可以当容器）
    fx_canvas_new(pixel("6,32", "444,196"), name("gfx_cv"), page(1), anim(1),
                  color(FX_RGB(30, 30, 30)), call(on_gfx))
    fx_parent(fx_find("gfx_cv"))
    fx_button_new(pixel("20,40", "90,64"), page(1), title("内嵌"), color(FX_RGB(255, 152, 0)), call(on_mode))
    fx_parent(fx_find("tab"))
    fx_button_new(pixel("6,202", "110,236"), name("sh_btn"), page(1), title("切换模式"),
                  color(FX_BTN_BLUE), call(on_mode))
    fx_slider_new(pixel("120,208", "300,222"), name("spin"), page(1), value(S.spin),
                  color(FX_RGB(244, 67, 54)), call(on_spin))
    fx_label_new(pixel("310,204", "444,236"), name("gfx_info"), page(1), title("模式: 全部"),
                 fgcolor(FX_RGB(51, 51, 51)))

    -- 页2 控件
    fx_grid_map(pixel("8,54", "252,214"), line(3), row(3), name("keys"), page(2), dense())
    for i = 0, 8 do
        fx_button_new(grid("keys", i // 3 + 1, i % 3 + 1, i // 3 + 1, i % 3 + 1),
                      title(KEYS[i + 1]), page(2), color(FX_BTN_BLUE),
                      call(function(w) on_key(w, i) end))
    end
    fx_label_new(pixel("260,36", "462,50"), page(2), title("颜色选择器"), fgcolor(FX_RGB(51, 51, 51)))
    fx_label_new(pixel("260,56", "304,70"), page(2), title("背景"), fgcolor(FX_RGB(120, 120, 120)))
    fx_textedit_new(pixel("260,72", "328,90"), name("cp_br"), page(2), title("255"), call(cp_cb))
    fx_textedit_new(pixel("332,72", "398,90"), name("cp_bg"), page(2), title("90"), call(cp_cb))
    fx_textedit_new(pixel("406,72", "462,90"), name("cp_bb"), page(2), title("0"), call(cp_cb))
    fx_label_new(pixel("260,96", "304,110"), page(2), title("文字"), fgcolor(FX_RGB(120, 120, 120)))
    fx_textedit_new(pixel("260,112", "328,130"), name("cp_tr"), page(2), title("40"), call(cp_cb))
    fx_textedit_new(pixel("332,112", "398,130"), name("cp_tg"), page(2), title("40"), call(cp_cb))
    fx_textedit_new(pixel("406,112", "462,130"), name("cp_tb"), page(2), title("40"), call(cp_cb))
    fx_button_new(pixel("260,142", "462,164"), page(2), title("示例按钮"), name("cp_btn"),
                  color(FX_RGB(255, 90, 0)), call(cp_cb))
    fx_label_new(pixel("260,172", "462,192"), page(2), name("cp_sw"), title("颜色显示区"),
                 fgcolor(FX_WHITE), color(FX_RGB(255, 90, 0)))
    fx_button_new(pixel("260,200", "348,220"), page(2), title("重置"), color(FX_RED_ACCENT), call(on_reset))
    fx_label_new(pixel("260,224", "462,236"), page(2), name("info"), title("点击数字键试试"),
                 fgcolor(FX_RGB(51, 51, 51)))
    fx_panel_new(pixel("254,26", "466,242"), page(2), color(FX_RGB(250, 250, 250)), border(1))

    -- 页3 图片
    fx_canvas_new(pixel("6,32", "280,220"), name("imgq"), page(3), anim(1),
                  color(FX_RGB(245, 245, 245)), call(on_imgq))
    fx_button_new(pixel("292,36", "444,58"), name("img_load"), page(3), title("导入图片…"),
                  color(FX_BTN_BLUE), call(on_img_load))
    fx_button_new(pixel("292,64", "366,86"), page(3), title("换图案"), call(on_img))
    fx_button_new(pixel("372,64", "444,86"), page(3), title("复位"), color(FX_RED_ACCENT), call(on_img_reset))
    fx_label_new(pixel("292,92", "444,110"), page(3), title("缩放 (以中心为基准)"),
                 fgcolor(FX_RGB(51, 51, 51)))
    fx_slider_new(pixel("292,112", "444,132"), name("zoom"), page(3), value(30),
                  color(FX_BTN_BLUE), call(on_zoom))
    fx_label_new(pixel("292,142", "444,238"), name("img_info"), page(3),
                 title("拖动四角顶点可任意形变"), fgcolor(FX_RGB(51, 51, 51)))

    -- 页4 3D
    fx_canvas_new(pixel("6,32", "444,196"), name("rt_cv"), page(4), anim(1),
                  color(FX_BLACK), call(on_3d))
    fx_canvas_set_buf(fx_find("rt_cv"), 0)
    fx_button_new(pixel("6,202", "76,236"), page(4), title("超频: 关"), color(FX_RED_ACCENT), call(on_oc))
    fx_button_new(pixel("82,202", "152,236"), page(4), title("GPU: 开"), color(FX_RGB(76, 175, 80)), call(on_gpu))
    fx_slider_new(pixel("160,204", "250,218"), name("rspeed"), page(4), value(S.rspeed),
                  color(FX_BTN_BLUE), call(on_rspeed))
    fx_slider_new(pixel("160,220", "250,234"), name("rqual"), page(4), value(S.rqual),
                  color(FX_RGB(76, 175, 80)), call(on_rqual))
    fx_label_new(pixel("258,202", "444,236"), name("fps_lbl"), page(4), title("FPS: --"),
                 fgcolor(FX_RGB(51, 51, 51)))

    -- 页5 输入
    fx_label_new(pixel("6,36", "240,56"), page(5), title("点击输入框, 直接打字:"),
                 fgcolor(FX_RGB(51, 51, 51)))
    fx_textedit_new(pixel("6,60", "444,140"), name("edit1"), page(5), title("hello 你好"))
    fx_textedit_new(pixel("6,148", "444,182"), name("edit2"), page(5), title(""), maxlen(20))
    fx_label_new(pixel("6,190", "444,236"), page(5),
                 title("拖拽框选 / Ctrl+A C V X / 方向键 Home End;\n第一个框不限字数, 第二个限 20 字(带计数)。"),
                 fgcolor(FX_RGB(120, 120, 120)))

    -- 页6 画板
    fx_canvas_new(pixel("6,32", "330,236"), name("paint_cv"), page(6), anim(1),
                  color(FX_WHITE), call(on_paint))
    fx_button_new(pixel("340,40", "400,70"), name("br_r"), page(6), title("红"),
                  color(FX_RGB(244, 67, 54)), call(function() on_brush(FX_RGB(244, 67, 54)) end))
    fx_button_new(pixel("410,40", "444,70"), name("br_g"), page(6), title("绿"),
                  color(FX_RGB(76, 175, 80)), call(function() on_brush(FX_RGB(76, 175, 80)) end))
    fx_button_new(pixel("340,80", "400,110"), name("br_b"), page(6), title("蓝"),
                  color(FX_BTN_BLUE), call(function() on_brush(FX_BTN_BLUE) end))
    fx_button_new(pixel("410,80", "444,110"), name("br_k"), page(6), title("黑"),
                  color(FX_RGB(40, 40, 40)), call(function() on_brush(FX_RGB(40, 40, 40)) end))
    fx_button_new(pixel("340,120", "444,150"), page(6), title("清空"),
                  color(FX_RGB(150, 150, 150)), call(on_clear))

    -- 页7 键鼠
    fx_canvas_new(pixel("6,32", "444,236"), name("keys_cv"), page(7), anim(1),
                  color(FX_RGB(30, 30, 30)), call(on_keys_view))

    -- 页8 压测
    fx_canvas_new(pixel("6,32", "444,236"), name("move_cv"), page(8), anim(1),
                  color(FX_RGB(20, 20, 26)), call(on_move))
    fx_parent(fx_find("move_cv"))
    fx_button_new(pixel("100,100", "220,136"), name("mv_btn"), page(8), title("飞天按钮"),
                  color(FX_BTN_BLUE), call(on_mv_click))
    fx_label_new(pixel("100,150", "200,170"), name("mv_lbl"), page(8), title("漂移标签"),
                 fgcolor(FX_YELLOW))
    fx_progress_new(pixel("100,180", "260,194"), name("mv_prog"), page(8), value(0))
    fx_checkbox_new(pixel("100,200", "210,222"), name("mv_chk"), page(8), title("勾选机"),
                    fgcolor(FX_WHITE))
    fx_parent(fx_find("tab"))

    -- 页9 滚动
    fx_canvas_new(pixel("6,32", "444,236"), name("scroll_cv"), page(9),
                  color(FX_RGB(245, 245, 245)), call(on_scroll_view))

    -- 页10 组件
    fx_label_new(pixel("8,30", "180,44"), page(10), title("名字列表"), fgcolor(FX_RGB(40, 40, 40)))
    fx_grid_map(pixel("8,50", "180,70"), line(1), row(5), name("ncedit"), dense(), page(10))
    fx_textedit_new(grid("ncedit", 1, 1, 1, 4), name("nc_txt"), page(10), title(""))
    fx_button_new(grid("ncedit", 1, 5, 1, 5), page(10), title("+"), color(FX_BTN_BLUE), call(on_nc_add))
    S.nclist = fx_list_new_p("8,76", "180,180", 10)
    names_refresh()
    if S.nclist then fx_list_set_cb(S.nclist, on_nc_pick) end
    fx_button_new(pixel("8,184", "88,204"), page(10), title("删除"), color(FX_RED_ACCENT), call(on_nc_del))
    fx_label_new(pixel("200,30", "460,44"), page(10),
                 title("原内容: 可以自由拖动的组件(不限于按钮)"), fgcolor(FX_RGB(40, 40, 40)))
    fx_canvas_new(pixel("200,48", "460,120"), name("drag_cv"), page(10), anim(1),
                  color(FX_RGB(245, 245, 245)), call(on_drag))
    fx_button_new(pixel("200,126", "330,148"), page(10), title("选择文件夹预览"),
                  color(FX_BTN_BLUE), call(on_fs_pick))
    fx_label_new(pixel("310,130", "460,144"), name("folder_lbl"), page(10), title("(未选择)"),
                 fgcolor(FX_RGB(40, 40, 40)))
    fx_canvas_new(pixel("200,152", "460,236"), name("fs_cv"), page(10), anim(1),
                  color(FX_RGB(250, 250, 250)), call(on_fs_view))
    fx_label_new(pixel("8,210", "180,224"), page(10), name("font_demo"), title("示例缩放文本"),
                 fgcolor(FX_RGB(40, 40, 40)))
    fx_slider_new(pixel("8,226", "180,240"), page(10), value(10), call(on_font_scale))
    fx_label_new(pixel("8,244", "180,258"), page(10), name("nc_picked"), title("选中: -"),
                 fgcolor(FX_RGB(120, 120, 120)))

    -- 页11 粒子
    fx_canvas_new(pixel("6,32", "444,196"), name("pt_cv"), page(11), anim(1),
                  color(FX_BLACK), call(on_pt))
    fx_slider_new(pixel("6,204", "300,220"), page(11), value(15), call(on_pt_slider))
    fx_label_new(pixel("306,204", "444,220"), page(11), title("拖动调粒子数(×40)"),
                 fgcolor(FX_RGB(51, 51, 51)))

    fx_parent(nil)
end

-- ==================== 入口 ====================

function app_init()
    fxtk_set_fps_debug(0)
    fx_set_touch_debug(0)
    fx_set_bg(FX_WINDOW_BG)
    fx_set_window_title(string.format("fxtk v%s · Lua demo (%s)", fxtk_version or "2.5.3", fx_backend_name()))

    for i = 0, 2 do S.pics[i + 1] = make_pic(i) end
    if S.pics[2] then fx_image_grayscale(S.pics[2]) end
    if S.pics[3] then fx_image_tint(S.pics[3], FX_RGB(0, 200, 255), 90) end

    build_ui()

    local page = os.getenv("FXTK_PAGE") or os.getenv("FXTK_LUA_PAGE")
    if page then
        local n = tonumber(page)
        if n then fx_set_value(fx_find("tab"), n) end
    end
end

function app_frame(dt, frame)
    -- 演示本身的动画都在 canvas 回调里；这里保留钩子，方便脚本加全局逻辑。
end
