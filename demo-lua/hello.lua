-- 最小烟雾测试：证明 C 核心 + Lua 语法层 + sokol 宿主整条链路可用
require("fxtk").install_globals()

local clicks = 0

function app_init()
    fx_set_bg(FX_WINDOW_BG)
    fx_label_new(pixel("16,12", "464,40"), title("Hello, fxtk + Lua!"), fgcolor(FX_RGB(40, 40, 40)))
    fx_button_new(pixel("16,56", "200,96"), name("ok"), title("点我"), call(function(w)
        clicks = clicks + 1
        fx_set_title(w, "已点击 " .. clicks)
    end))
    fx_canvas_new(pixel("16,112", "464,248"), name("cv"), anim(1), color(FX_CANVAS_DARK), call(function(w)
        local cw, ch = fx_canvas_size(w)
        fx_set_color(0x101410)
        for x = 0, cw - 1, 16 do fx_draw_vline(x, 0, ch - 1) end
        for y = 0, ch - 1, 16 do fx_draw_hline(0, cw - 1, y) end
        fx_set_color(FX_YELLOW)
        fx_draw_circle(cw // 2, ch // 2, 24)
        fx_set_color(FX_RED)
        fx_fill_circle(cw // 2, ch // 2, 8)
        fx_draw_text(12, 10, "Lua canvas 回调")
    end))
end
