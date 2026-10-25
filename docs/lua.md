# Lua 绑定（fxtk + Lua 5.4）

> **核心：fxtk-pc-v2.5（C99，`FXTK_VERSION` = 2.5.4，语义未改）。**
> Lua 不是重写版，而是绑定层：控件、画布、图片、输入、后端服务仍由 C 核心实现；
> 本仓库多做了一件事——让同一套 API 可以被 Lua 5.4 直接调用，并提供一个 12 页的 Lua 演示。

- 绑定源码：`bindings/lua/fxtk_lua.c` / `fxtk_lua.h`
- 语法层：`lua/fxtk.lua`（C 属性宏同构写法 + `install_globals()`）
- Lua 5.4.6：`third_party/lua/`（vendored 源码，编进 exe，**不需要系统 liblua**）
- Lua 演示：`demo-lua/app.lua`（12 页）；`demo-lua/hello.lua`（最小冒烟）
- 宿主：`drivers/main_lua.c`（sokol 窗口版）、`demo-main/test/lua_host_sdl.c`（无头 SDL dummy 版）

---

## 1. 构建与运行

### Linux（sokol 窗口版）

```bash
make -C demo-main lua        # 产物 demo-main/fxtk_lua
cd demo-main
./build.sh --lua             # 或一键构建并运行
./fxtk_lua                   # 默认加载 ../demo-lua/app.lua（同时尝试 ./demo-lua/app.lua、./app.lua）
FXTK_SCRIPT=../demo-lua/hello.lua ./fxtk_lua
FXTK_PAGE=3 ./fxtk_lua       # 启动后切到第 3 页（从 0 开始）
# 自动化：点击/拖拽注入
FXTK_CLICKS='281,84,10;100,330,20' ./fxtk_lua
FXTK_DRAG='250,300,650,450,12,20' ./fxtk_lua   # x0,y0,x1,y1,步数,帧号
```

脚本查找顺序：`FXTK_SCRIPT` 环境变量 → exe 同目录的 `demo-lua/app.lua` → 当前目录 `demo-lua/app.lua`
→ 当前目录 `app.lua`。找到后设置 `package.path`（脚本目录与 exe 目录），所以脚本可以
`require` 同目录/上级的模块。

### 无头版（测试、截图、CI）

```bash
make -C demo-main lua-sdl    # 产物 demo-main/test/lua_host_sdl
cd demo-main
SDL_VIDEODRIVER=dummy ./test/lua_host_sdl ../demo-lua/app.lua 30 3 /tmp/lua_p3.png
# 参数：[脚本] [帧数] [页] [截图路径]；也支持环境变量
#   FXTK_SCRIPT / FXTK_FRAMES / FXTK_PAGE / FXTK_SHOT / FXTK_WIN_W / FXTK_WIN_H
```

无头宿主会先推一个 1280x720 的 resize 事件，再跑 N 帧，最后调用 `fx_screenshot`；
Lua 运行错误会让进程返回非 0（供 CI 使用）。

### Windows 单 exe

```bash
cd demo-main
./build_win_sokol.sh lua     # dist/win_sokol/fxtk_lua.exe（内嵌 Lua，只有系统 DLL）
```

### 无头绑定自检

```bash
make -C demo-main lua-test
# 期望输出：[lua-test] OK: attrs/widgets/callbacks/canvas/image/list/flex/svg/file/gesture
```

`test/lua_test.lua` 覆盖：颜色/属性表、控件创建/查找/取值、`call` 回调、`fx_set_cb` 动态回调、
列表回调（带 index）、画布回调与绘制、图片创建/读写/线/特效、列表增删、flex、SVG、文件/路径、
时间/随机/后端信息、手势，以及删除/重复查找。

---

## 2. 宿主契约

脚本只需要两个全局函数（都不是必须同时存在）：

```lua
function app_init()              -- 创建控件树；fx_init 之后调用一次
end

function app_frame(dt, frame)    -- 可选；每帧在 fx_poll 之前调用
end
```

- `dt` 是秒（浮点），`frame` 从 0 开始递增。
- `fx_poll()` 由宿主调用，不要在脚本里再调一次。
- 控件回调由 `fx_poll` 内部触发；回调里可以安全地改控件属性、切换页、增删控件。
- 退出时宿主 `lua_close`；仍在控件树里的图片 userdata 会走 `__gc` 释放。

---

## 3. 语法：和 C 的属性宏一一对应

```lua
require("fxtk").install_globals()

fx_label_new(pixel("16,12", "464,40"), title("设置"))
fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"),
              color(FX_BTN_BLUE), call(function(w)
                  fx_log(FX_LOG_INFO, "clicked")
              end))
```

C 与 Lua 的对应：

| C | Lua |
|---|---|
| `pixel("x,y","x2,y2")` | `pixel("x,y","x2,y2")`（同名字符串） |
| `percent("a,b","c,d")` | `percent(...)` |
| `grid("name", r1,c1,r2,c2)` | `grid("name", r1,c1,r2,c2)`，也支持可变参数 |
| `title("...")` / `text("...")` | 同名 |
| `call(cb)` | `call(function(w) ... end)` |
| `page(n)` / `anim(n)` / `dense()` / `value(n)` | 同名 |
| `color(FX_RGB(...))` / `fgcolor(...)` / `border(n)` / `radius(n)` | 同名 |
| `line(n)` / `row(n)` / `sidebar(FX_TAB_LEFT)` / `maxlen(n)` / `image(img)` | 同名 |
| `fx_button_new(pixel(...), ...)` | `fx_button_new(pixel(...), ...)`（C API 名原样保留） |
| `fx_find("name")` | 同名，返回 lightuserdata（或 nil） |
| `fx_widget_rect(w,&x1,&y1,&x2,&y2)` | `local x1,y1,x2,y2 = fx_widget_rect(w)` |
| `fx_canvas_size(w,&w,&h)` | `local cw,ch = fx_canvas_size(w)` |
| `fx_touch_state(&x,&y,&p)` | `local x,y,p = fx_touch_state()` |
| `fx_last_key(&k)` | `local k = fx_last_key()`（表：`k.key`、`k.utf8`） |
| `fx_gesture_poll(&ev)` | `local ev = fx_gesture_poll()`（表或 nil） |
| `fx_image_size(img,&w,&h)` | `local w,h = fx_image_size(img)` |

属性本身也是 Lua 函数，返回带 `__fx` 标记的 table；控件构造函数接受 `...` 个属性。
不传属性时等价于 C 的默认值。

### 3.1 也可以不用全局名

```lua
local fx = require("fxtk")
local core = fx.core
core.fx_label_new(fx.pixel("16,12", "200,40"), fx.title("hi"))
```

`install_globals()` 只是把命中的名字装进 `_G`（`fx_*`、`fxtk_*`、`FX_*` 以及属性名），
方便从 C 示例直接平移；生产脚本可以只用局部表。

---

## 4. 回调

### 4.1 普通控件

```lua
local n = 0
fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"), call(function(w)
    n = n + 1
    fx_set_title(w, "点了 " .. n .. " 次")
end))
```

- `w` 是控件句柄（lightuserdata）。
- 回调在绘制/事件阶段触发；**不要**在回调里调用 `fx_init` / `fx_shutdown`。
- 回调抛错会被 `pcall` 捕获，打印 traceback 到 `fx_log(FX_LOG_ERROR, ...)`，不会让 C 崩溃；
  但该回调之后的 Lua 逻辑不会执行——生产代码请自行 `pcall` 包住易错部分。

### 4.2 列表 / 下拉

```lua
fx_list_set_cb(list, function(w, idx)
    print("选中第 " .. idx .. " 项")
end)
```

`idx` 从 0 开始；这是列表专用 trampoline（`lua_list_cb`），和普通控件回调分开。
下拉（`fx_drop_*`）在 C 核心内部用 `fx_set_cb(w, ex_drop_cb, 0)` 实现，**没有**单独的选择回调；
Lua 侧不要对列表/下拉调用 `fx_set_cb` 覆盖内部回调（列表用 `fx_list_set_cb`）。

### 4.3 动态替换回调

```lua
fx_set_cb(btn, function(w) ... end)
```

C 的 `fx_set_cb(w, cb, ud)` 里的 `ud` 不暴露成 Lua userdata；Lua 侧用注册表保存函数
（`fxtk.cbs` 表，键是控件指针），所以 `fx_set_cb(w, fn)` 只需要两个参数。

### 4.4 删除与 GC

- `fx_delete(w)` 接受控件句柄、控件名字符串或 grid/属性 table；会清掉该控件在 Lua 注册表里的
  回调/图片引用。
- **不递归**删除子控件：先删子件再删父件，或先 `fx_parent(nil)` 调整树。
- 删除后不要再使用旧句柄；Lua 侧不会自动置 nil。

---

## 5. 图片 / SVG

```lua
local img = fx_image_create(120, 90)      -- 返回 userdata，__gc 释放
for y = 0, 89 do
    for x = 0, 119 do
        fx_image_set_px(img, x, y, FX_RGB(x, y, 100))
    end
end
fx_image_widget = fx_image_new(pixel("10,10", "130,100"), image(img))

-- 便捷函数（绑定层补充，C 里没有同名）
local w, h = fx_image_size(img)
fx_image_fill(img, FX_WHITE)
fx_image_line(img, 0, 0, 119, 89, FX_RED, 2)
local c = fx_image_get_px(img, 10, 10)

-- SVG
local svg = fx_svg_parse("<svg ...>...</svg>")
fx_svg_draw(svg, 10, 10, 100, 100)
```

- 用 `image(img)` 属性或 `fx_set_image(w, img)` 把图片挂到控件上时，绑定层会在
  `fxtk.imgrefs` 里保留引用，防止 Lua GC 先释放；删控件时自动解除。
- 手动绘制的图片（例如画板/粒子）只由 Lua 变量持有；置 nil 后等 GC 即可。
- 图片绘制函数接受 userdata；传错类型会抛 Lua error（带 `fx_image_*` 名字提示）。

---

## 6. 演示 `demo-lua/app.lua`

与 C 版 `demo-main/app.c` + `app_desktop.c` 同页、同布局、同交互（部分热路径限流）：

| 页 | 内容 |
|---|---|
| 0 波形 | 画布波形 + 速度滑块/进度条 + 开关 |
| 1 图形 | 旋转图片、六边形/三角形、拖尾、圆角/圆弧、伪 3D 四边形形变 |
| 2 控件 | 3x3 数字键、颜色选择器（三组 RGB 输入框）、示例按钮、面板 |
| 3 图片 | 内置 3 张程序化图片、导入（`FXTK_IMPORT` / 文件对话框）、四角拖动形变、缩放 |
| 4 3D | GPU raymarch（宿主支持时）；否则 Lua 回退动画；超频/GPU/速度/质量 |
| 5 输入 | 两个文本框（不限字数 / 限 20 字计数） |
| 6 画板 | 图片像素缓冲 + 画笔颜色 + 清空 |
| 7 键鼠 | 键码/UTF-8/鼠标坐标/事件日志/十字标 |
| 8 压测 | 动态按钮增删、漂移、FPS 富余时每帧 +1（Lua 版封顶 1200） |
| 9 滚动 | 自绘列表 + 滚轮 + 滚动条 |
| 10 组件 | 列表增删、拖拽画布、文件夹浏览、字号缩放 |
| 11 粒子 | Lua 粒子缓冲（封顶 4000）+ 数量滑块 |

页签切换：`fx_set_value(fx_find("tab"), n)`；无头/窗口宿主都支持 `FXTK_PAGE=n`。

### 截图与逐像素对比

```bash
cd demo-main
# Lua 无头
SDL_VIDEODRIVER=dummy ./test/lua_host_sdl ../demo-lua/app.lua 30 2 /tmp/lua_p2.png
# C 无头基准（同页、同尺寸、同帧数）
SDL_VIDEODRIVER=dummy ./test/bench 30 2 1000 1280 720 /tmp/c_p2.png
# 比较
python3 - <<'EOF'
from PIL import Image; import numpy as np
a=np.array(Image.open('/tmp/c_p2.png').convert('RGB')).astype(int)
b=np.array(Image.open('/tmp/lua_p2.png').convert('RGB')).astype(int)
d=np.abs(a-b); print('max', d.max(), 'ndiff', int((d.sum(2)>0).sum()))
EOF
```

本机实测（1280x720、30 帧、SDL dummy 软件渲染）：

| 页面 | 差异像素 / 921600 | 说明 |
|---|---|---|
| 2 控件、3 图片、5 输入、6 画板、9 滚动、10 组件 | 1 | 仅截图右下/左下边缘 1 像素，可视为逐像素一致 |
| 0 波形、1 图形 | 约 1000 | 动画帧相位/浮点 `sinf` 差异 |
| 7 键鼠 | 动态 | 事件日志含运行时刻，不做像素比对 |
| 8 压测、11 粒子 | 动态 | 帧率/随机数决定动态元素，不做像素比对 |
| 4 3D | 动态 | GPU raymarch 画面随帧变化 |

> 对比命令与数字是为了证明“Lua 版不是另画一套”，不是金图回归门禁；CI 目前只跑
> `make lua-test` 与构建，不做 Lua 截图逐像素断言。

---

## 7. 从 C 迁移到 Lua

1. 把 `#include "fxtk.h"` 换成 `require("fxtk").install_globals()`。
2. `static void on_x(fx_widget_t *w, void *ud)` → `local function on_x(w)`；
   需要额外状态就闭包捕获或放进模块表。
3. `void app_init(void)` → `function app_init()`；`fx_poll()` / 主循环交给宿主。
4. 多输出参数改成多返回值（见上表）。
5. 图片 `fx_image_t*` → userdata；数组/指针改成 Lua table 或逐像素 API。
6. 字符串格式沿用 `string.format`；中文注释/标题直接写 UTF-8 字符串。

### 一个最小可运行脚本

```lua
-- hello.lua
require("fxtk").install_globals()

local clicks = 0
local function on_ok(w)
    clicks = clicks + 1
    fx_set_title(fx_find("info"), "点击 " .. clicks .. " 次")
end

function app_init()
    fx_set_bg(FX_WINDOW_BG)
    fx_label_new(pixel("16,12", "464,40"), title("fxtk Lua"))
    fx_button_new(pixel("16,56", "200,96"), name("ok"), title("确定"),
                  color(FX_BTN_BLUE), call(on_ok))
    fx_label_new(pixel("16,104", "300,128"), name("info"), title("点一下"))
end
```

---

## 8. 已知边界（如实写）

- **不是把 C 核心改写成 Lua**：控件池、布局、绘制、输入、后端服务仍在 C 里；Lua 侧只是调用。
- **性能**：每次回调/绘制 API 调用有跨语言开销。Lua 版演示对粒子（4000）和压测动态控件（1200）
  做了封顶；需要极限性能时优先在 C 侧加批量 API，而不是在 Lua 里循环。
- **GPU/CPU 光追**：`fx_draw_raymarch` 只在有对应后端钩子的宿主可用；Lua 演示在不可用宿主上
  画回退动画，没有把 `demo-main/raymarch.c` 的 CPU 实现移植到 Lua。
- **窗口 API**：无头 SDL 宿主编译时定义 `FXTK_LUA_NO_WINAPI`，`fx_win_*` 是桩；sokol 宿主
  链接 `drivers/fxtk_window.c`，Windows 版包含真实 Win32 实现。
- **Windows 运行时**：本环境只验证了交叉编译与产物依赖（只有系统 DLL），没有在真实 Windows
  上跑完整交互；wine 下的 sokol 窗口/GL 不可用，不能作为运行时证据。
- **未覆盖**：Lua 协程与绘制回调混用、热重载脚本、输入法全链路、多 Lua state；
  这些都不在本次绑定范围内。
- **API 覆盖**：`fxtk.h` 的主要公开 API 都有绑定；**未绑定**的有：`fx_wm_*`（画布内窗口管理器）、
  `fx_par_for` / `fx_parallel_for`（C 线程回调，跨 Lua state 不安全）、CPU raymarch 的 C 实现、
  旧 `fx_fs_*` 文件接口；`fxtk_font_height` 头文件有声明但无后端实现，因此没有暴露。
  `fx_init` / `fx_poll` 由宿主负责，不在脚本里调用。完整清单见
  `bindings/lua/fxtk_lua.c` 的 `luaopen_fxtk_core`。

---

## 9. 常见问题

**Q: 为什么 `call` 里的 Lua 报错没有让程序退出？**
A: 绑定层用 `lua_pcall` + traceback 捕获，错误写入 `fx_log`；这是为了让 UI 线程不被脚本错误带崩。
调试时看 stderr / `fx.log`。

**Q: 为什么列表点击没触发？**
A: 列表/下拉回调是“松手后的下一次绘制”触发（沿用 C 的 `was_pr` 逻辑）；不要在同一次事件里等回调。

**Q: 控件删了 Lua 变量还要置 nil 吗？**
A: 建议置 nil；控件句柄本身不持有 Lua 引用，但回调注册表里会保留函数，直到 `fx_delete` 清理。

**Q: 能用 `require` 拆模块吗？**
A: 可以。宿主已把脚本目录加入 `package.path`；模块里同样 `require("fxtk")` 后使用即可。

---

## 10. 相关文档

- [属性宏语法与边界](syntax.md)
- [API 速查](api.md)
- [内部机制](internals.md)
- [后端服务层](backends.md)
- [Windows 编译](windows.md)
