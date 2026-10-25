# demo-lua

Lua 版 12 页演示。核心仍是 **fxtk-pc-v2.5（C99）**；这里只有脚本，没有第二套控件实现。

- `app.lua` — 完整演示（波形/图形/控件/图片/3D/输入/画板/键鼠/压测/滚动/组件/粒子）
- `hello.lua` — 最小冒烟脚本

## 运行

```bash
cd demo-main
make lua
./fxtk_lua_runner                  # 运行当前目录 app.lua（无则 ../demo-lua/app.lua）
FXTK_SCRIPT=../demo-lua/hello.lua ./fxtk_lua_runner
FXTK_PAGE=3 ./fxtk_lua_runner      # 直接打开第 3 页（0 基）
```

无头截图/CI：

```bash
make lua-sdl
SDL_VIDEODRIVER=dummy ./test/lua_host_sdl ../demo-lua/app.lua 30 3 /tmp/lua_p3.png
```

绑定自检：

```bash
make lua-test
```

Windows 单 exe：

```bash
cd demo-main && ./build_win_sokol.sh lua
# dist/win_sokol/fxtk_lua.exe / fxtk_lua_runner.exe（同一程序）
```

## 写自己的页面

```lua
require("fxtk").install_globals()

function app_init()
    fx_label_new(pixel("16,12", "464,40"), title("我的页面"))
    fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"),
                  call(function(w) fx_set_title(w, "ok") end))
end
```

API 映射和边界见 [docs/lua.md](../docs/lua.md)。
