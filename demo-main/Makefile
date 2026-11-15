# fxtk 统一构建入口 (v2.3.1 恢复)
#
#   make              # 构建完整演示 fxtk_sim (12 页)
#   make lua          # 构建通用 runner fxtk_lua_runner (C 核心 + 内嵌 Lua 5.4；脚本运行时加载)
#   make lua-test     # 无头运行 Lua 绑定自检 (SDL dummy)
#   make fxtk_sim_en  # 英文演示 (侧边栏标签)
#   make test         # 构建并运行无头单元测试 (无需 SDL/窗口/字体)
#   make check        # 质量闸门: 无头测试 + 模块自检 + 裁剪一致性 + API 符号/文档 + 版本号 + 脚本权限 (v2.5.6)
#   make test-asan    # ASan+UBSan 跑无头测试 (v2.5.6, CI 也跑)
#   make ex01_hello   # 构建指定示例: examples/ 下任一 ex*.c
#   make canvas_01_primitives   # 构建画布教程: examples/canvas/ 下任一 canvas_*.c
#   make clean
#
# 注意: `test` 目标显式声明了 .PHONY 且有真实规则 ——
#       避免再次踩 "同名 test/ 目录被 make 当作已是最新" 的假成功坑。
.PHONY: all test check api-check version-check mode-check test-asan clean fxtk_sim fxtk_sim_en fxtk_sim_sokol fxtk_sim_sdl fxtk_sim_sdl_en bench sokol lua lua-runner lua-test lua-sdl
.DEFAULT_GOAL := fxtk_sim

# v2.3: 体积优化默认档 (-O2 基线 154KB → 101KB, -35%)
#   -Os 优化体积 / LTO+gc-sections 跨编译单元死代码消除 /
#   去 unwind 表(C 无异常, 失去回溯栈)与 .comment/.note 构建标识。
#   需要 -O2 调试时: make CFLAGS="-O2 -g" LDFLAGS= -DNDEBUG
CFLAGS ?= -Os -s -flto -ffunction-sections -fdata-sections \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -fno-stack-protector -fno-ident
# v2.4 体积: --gc-sections 已开; 再用 gold 的 --icf=all(相同代码折叠) + --as-needed 压掉冗余
# (体积目标 Linux ≤150KB; 普通 bfd 链接器不支持 --icf, 探测不到 gold 就退回原参数)
LD_GOLD := $(shell command -v ld.gold >/dev/null 2>&1 && echo yes)
ifeq ($(LD_GOLD),yes)
LDFLAGS ?= -Wl,--gc-sections -Wl,--build-id=none -Wl,--icf=all -Wl,--as-needed -fuse-ld=gold
else
LDFLAGS ?= -Wl,--gc-sections -Wl,--build-id=none
endif
INC    := -I. -I../components/fxtk -I../drivers
# v2.5.6: 增量构建中间产物目录 (.o)
BUILD := build

# demo 链接 (3D 页需要 EGL/GLES/X11)
LIBS    := -lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lEGL -lGLESv2 -lX11
# 示例链接 (不含 GPU 光追, 无需 EGL)
LIBS_EX := -lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lX11

# ---- 核心库单一源清单 (demo / examples / test 共用) ----
# v2.3.1: 补上 fxtk_backends.c (fx_fs_pick_dir/fx_fs_list, app_desktop.c 与 canvas_08 依赖)。
# 此前 build.sh/build_ex.sh/build_win.sh 三处清单都漏了它, 链接必败。
CORE := ../components/fxtk/fxtk.c \
        ../components/fxtk/fxtk_draw.c \
        ../components/fxtk/fxtk_widgets.c \
        ../components/fxtk/fxtk_font.c \
        ../components/fxtk/fxtk_effects.c \
        ../components/fxtk/fxtk_extra.c \
        ../components/fxtk/fxtk_backends.c \
        ../components/fxtk/fxtk_par.c \
        ../components/fxtk/fxtk_gesture.c \
        ../components/fxtk/fxtk_flex.c \
        ../components/fxtk/fxtk_svg.c
HDRS := ../components/fxtk/fxtk.h ../components/fxtk/fxtk_internal.h
DRIVER := ../drivers/fxtk_sdl_driver.c ../drivers/main_linux.c

# ---- v2.5.6: 所有源清单与对象清单集中在此定义 ----
# GNU make 的目标前置依赖在【解析时】就展开, 所以这些变量必须出现在任何使用它们的
# 目标之前 (此前把 CORE_OBJ 定义在 fxtk_sim 之后, 导致 $^ 为空、链接报 main 未定义)。
# ---- v2.4 sokol 后端 (无 SDL 依赖; 文本层走 stb_truetype) ----
SOKOL_INC := -I../third_party/sokol
SOKOL_LIBS := -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
# ⚠️ 这份清单【不是】CORE 的拷贝, 而是独立维护的(去掉 fxtk_font.c, 改用
#    fxtk_font_stb.c)。新增核心 .c 时【两处都要加】—— v2.5 初版只往 CORE 里加了
#    fxtk_gesture.c, 结果 sokol 版演示报 "fx_gesture_feed 未定义的引用"。
SOKOL_CORE := ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
              ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
              ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c \
              ../components/fxtk/fxtk_par.c ../components/fxtk/fxtk_gesture.c \
              ../components/fxtk/fxtk_flex.c ../components/fxtk/fxtk_svg.c
# ---- v2.5: 单窗口桌面行为 (去标题栏/置顶/移动/居中/拖动) ----
# 独立成文件而不是塞进驱动: 它只用 sokol_app 暴露的原生窗口句柄(sapp_x11_get_window
# 等), 完全不碰渲染 —— 所以与帧流程、命令流、金图都无关, 加进来不影响既有行为。
WIN_SRC := ../drivers/fxtk_window.c
# ---- v2.5.4: Lua 绑定源清单 ----
LUA_INC   := -I../third_party/lua/src
LUA_CORE  := $(filter-out ../third_party/lua/src/lua.c ../third_party/lua/src/luac.c,$(wildcard ../third_party/lua/src/*.c))
LUA_BIND  := ../bindings/lua/fxtk_lua.c
LUA_HOST  := ../drivers/main_lua.c
LUA_FLAGS := -Wno-format-truncation
# ---- v2.5.6: 增量构建对象清单 ----
ALL_CORE_HDRS := $(wildcard ../components/fxtk/*.h)
CORE_OBJ := $(patsubst ../components/fxtk/%.c,$(BUILD)/core/%.o,$(SOKOL_CORE))
SOKOL_GL_OBJ := $(BUILD)/drv/fxtk_sokol_driver.o $(BUILD)/drv/fxtk_font_stb.o $(BUILD)/drv/fxtk_window.o
SOKOL_DRV_OBJ := $(SOKOL_GL_OBJ) $(BUILD)/drv/main_sokol.o
APP_OBJ := $(BUILD)/app/app.o $(BUILD)/app/app_desktop.o $(BUILD)/app/raymarch.o $(BUILD)/app/gpu_raymarch_stub.o
APP_EN_OBJ := $(BUILD)/app/app_en.o $(BUILD)/app/app_desktop_en.o $(BUILD)/app/raymarch.o $(BUILD)/app/gpu_raymarch_stub.o
LUA_OBJ := $(patsubst ../third_party/lua/src/%.c,$(BUILD)/lua/%.o,$(LUA_CORE))
LUA_BIND_OBJ := $(BUILD)/lua/fxtk_lua.o $(BUILD)/lua/main_lua.o

# ---- v2.4 起【默认后端 = sokol】----
# SDL2 降为遗留: 只保留 SDL 驱动作为【无头测试/基准/金图】用的假驱动 (SDL_VIDEODRIVER=dummy),
# 因为 CI 里没有显示器, 而 sokol 需要真实 GL 上下文。发布用的演示程序一律走 sokol。
all: fxtk_sim

# 旧名字继续可用(脚本/文档里引用的是它), 现在只是别名 —— 产物仍是 ./fxtk_sim(sokol 版)
fxtk_sim_sokol: fxtk_sim

fxtk_sim: $(CORE_OBJ) $(SOKOL_DRV_OBJ) $(APP_OBJ)
	gcc $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SOKOL_LIBS)

fxtk_sim_en: $(CORE_OBJ) $(SOKOL_DRV_OBJ) $(APP_EN_OBJ)
	gcc $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SOKOL_LIBS)

# ---- 遗留: SDL 版演示 (仅用于对照/过渡; 新功能不再往里加) ----
fxtk_sim_sdl: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app.c app_desktop.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app.c app_desktop.c -o $@ $(LDFLAGS) $(LIBS)

fxtk_sim_sdl_en: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app_en.c app_desktop_en.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app_en.c app_desktop_en.c -o $@ $(LDFLAGS) $(LIBS)

# ---- 无头单元测试 ----
# 不链 ../drivers/fxtk_font.c: 测试文件自带文字/纹理桩 (无 SDL/字体依赖)。
# v2.4: 同时跑一份 stub 后端变体 (-DFXTK_BACKEND_STUB: 确定性时钟/定种子随机/内存文件系统),
#       保证测试结果不依赖宿主机的时钟、随机源与文件系统。
test: test/headless_test test/headless_test_stub
	./test/headless_test
	./test/headless_test_stub

TEST_CORE := $(filter-out ../components/fxtk/fxtk_font.c,$(CORE))
# v2.5: CORE 里现在有 fxtk_par.c(pthreads), 无头测试也要链 -lpthread。
# 注意它【必须】排在目标文件之后 —— ld 的库顺序敏感, 放前面会解析不到符号。
test/headless_test: test/headless_test.c $(TEST_CORE) $(HDRS)
	gcc -O2 $(INC) $(TEST_CORE) test/headless_test.c -o $@ -lm -lpthread

test/headless_test_stub: test/headless_test.c $(TEST_CORE) $(HDRS)
	gcc -O2 -DFXTK_BACKEND_STUB $(INC) $(TEST_CORE) test/headless_test.c -o $@ -lm -lpthread

# ---- v2.5.6: 质量闸门 ----
# 都是"能编译"骗不过去的检查, 跑在 CI 里 (原先 README 声称的 ASan 干净、脚本可执行、
# 版本号一致, CI 一个都没验). 拆成独立目标, CI 里分步跑便于一眼定位。
api-check:      ## 公开头文件的每个声明都必须在所有字体后端里有实现
	python3 ../tools/check_symbols.py
api-doc-check:  ## 文档 ↔ 头文件一致性: 覆盖 + 参数个数 + 无幽灵符号 (v2.5.6)
	python3 ../tools/check_api_docs.py
version-check:  ## fxtk.h / CHANGELOG / README 三处版本号一致
	python3 ../tools/check_version.py
mode-check:     ## git 索引里所有 *.sh 都是 100755
	python3 ../tools/check_modes.py

# v2.5.6: 模块自检汇总 (全部无头/确定性) —— 并行 for / UI 缩放 / 触摸手势 / 弹性布局 / SVG 子集。
# CI 单独一步跑它, check 也带上 (此前这些自检只在本地手工跑, CI 从未验过)。
.PHONY: selftest
selftest: par-test uiscale-test gesture-test flex-test svg-test

check: test selftest trim-check api-check api-doc-check version-check mode-check

# ---- v2.5.6: ASan + UBSan 无头测试 ----
ASAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
test-asan:
	gcc $(ASAN_FLAGS) $(INC) $(TEST_CORE) test/headless_test.c -o test/headless_test_asan -lm -lpthread
	ASAN_OPTIONS=detect_leaks=1 ./test/headless_test_asan
	gcc $(ASAN_FLAGS) -DFXTK_BACKEND_STUB $(INC) $(TEST_CORE) test/headless_test.c -o test/headless_test_asan_stub -lm -lpthread
	ASAN_OPTIONS=detect_leaks=1 ./test/headless_test_asan_stub

# ---- v2.5.2: 多真实窗口 + 控件输入闭环 demo ----
# 一条命令构建: make mw_interactive && ./mw_interactive
.PHONY: mw_interactive
mw_interactive: ../examples/mw_interactive.c $(SOKOL_CORE) ../drivers/fxtk_sokol_mw.c ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/fxtk_win.c $(HDRS)
	gcc $(CFLAGS) -DFXTK_MW $(INC) $(SOKOL_INC) $(SOKOL_CORE) \
	    ../drivers/fxtk_sokol_mw.c ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/fxtk_win.c \
	    ../examples/mw_interactive.c -o $@ \
	    -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm

# ---- v2.5: 并行 for 自检 (无头, 不需要显示/窗口) ----
# 只链 fxtk_par.c, 不拖整个框架进来 —— 它不依赖任何渲染/驱动。
.PHONY: par-test
par-test: test/par_test
	./test/par_test

test/par_test: test/par_test.c ../components/fxtk/fxtk_par.c $(HDRS)
	gcc -O2 $(INC) test/par_test.c ../components/fxtk/fxtk_par.c -o $@ -lpthread

# ---- v2.5: UI 缩放 (HiDPI) 自检 ----
# 只链 fxtk_backends.c —— 它经核实只依赖 libc(外加 stb 需要的 -lm), 不拖框架进来。
.PHONY: uiscale-test
uiscale-test: test/uiscale_test
	./test/uiscale_test

test/uiscale_test: test/uiscale_test.c ../components/fxtk/fxtk_backends.c $(HDRS)
	gcc -O2 $(INC) -I../components/fxtk/vendor/stb test/uiscale_test.c \
	    ../components/fxtk/fxtk_backends.c -o $@ -lm

# ---- v2.5: 触摸手势自检 (无头, 合成触摸序列, 完全不依赖硬件/真实时钟) ----
.PHONY: gesture-test
gesture-test: test/gesture_test
	./test/gesture_test

test/gesture_test: test/gesture_test.c ../components/fxtk/fxtk_gesture.c $(HDRS)
	gcc -O2 $(INC) test/gesture_test.c ../components/fxtk/fxtk_gesture.c -o $@ -lm

# ---- v2.5: 弹性布局自检 (无头, 只链 fxtk_flex.c；它不依赖驱动/控件绘制) ----
.PHONY: flex-test
flex-test: test/flex_test
	./test/flex_test

test/flex_test: test/flex_test.c ../components/fxtk/fxtk_flex.c $(HDRS)
	gcc -O2 $(INC) test/flex_test.c ../components/fxtk/fxtk_flex.c -o $@ -lm

# ---- v2.5: SVG 子集自检 (无头; 绘制调用用录制器替身验, 不需要真驱动) ----
.PHONY: svg-test
svg-test: test/svg_test
	./test/svg_test

test/svg_test: test/svg_test.c ../components/fxtk/fxtk_svg.c $(HDRS)
	gcc -O2 $(INC) test/svg_test.c ../components/fxtk/fxtk_svg.c -o $@ -lm

# ---- sokol 后端 / 单窗口 / Lua 的源清单已上移到文件顶部 (解析时展开的要求) ----

# 单窗口行为自检。
# 【刻意不放进 test 目标】: 它要开真窗口并依赖窗口管理器, 不适合当无头 CI 的必过项。
# 无头/Xvfb 下它会如实报 [N/A]/[ENV] 而不是假绿 —— 这正是本项目要守住的原则。
.PHONY: window-test
window-test: test/window_test
	./test/window_test

test/window_test: test/window_test.c test/sokol_app_impl.c $(WIN_SRC) $(HDRS)
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) test/window_test.c test/sokol_app_impl.c \
	    $(WIN_SRC) -o $@ $(LDFLAGS) $(SOKOL_LIBS)

# ---- v2.5.4: Lua 绑定 (C 核心不变；Lua 只做脚本层) ----
# 三方 Lua 5.4.6 以源码形式 vendored (third_party/lua)，不依赖 liblua.so。
# fxtk_lua_runner = sokol 后端 + 内嵌 Lua + lua/fxtk.lua 语法层 的【通用 runner】:
#   脚本在运行时加载 —— fxtk_lua_runner              # 当前目录的 app.lua
#                    fxtk_lua_runner my.lua         # 指定脚本
#                    fxtk_lua_runner my_app/        # 目录 → <dir>/app.lua
#   改脚本不用重新编译; 只有改 C 绑定/核心才需要再 make lua。
# fxtk_lua 是同一个程序的兼容名(符号链接)，老命令/文档继续可用。
# (LUA_INC / LUA_CORE / LUA_BIND / LUA_FLAGS 已上移到文件顶部)

.PHONY: lua lua-runner lua-test lua-sdl lua-clean
lua: fxtk_lua_runner fxtk_lua
lua-runner: fxtk_lua_runner

lua-clean:
	rm -f fxtk_lua fxtk_lua_runner test/lua_host_sdl

fxtk_lua_runner: $(CORE_OBJ) $(SOKOL_GL_OBJ) $(LUA_BIND_OBJ) $(LUA_OBJ)
	gcc $(CFLAGS) $(LUA_FLAGS) $^ -o $@ $(LDFLAGS) $(SOKOL_LIBS)

# 兼容名: 同一份二进制(脚本在运行时加载)，老脚本/文档里的 ./fxtk_lua 继续可用。
fxtk_lua: fxtk_lua_runner
	ln -sf fxtk_lua_runner $@

# 无头 Lua 宿主 (SDL dummy)：跑脚本、截图、可进 CI。
lua-sdl: test/lua_host_sdl
test/lua_host_sdl: $(CORE) $(HDRS) ../drivers/fxtk_sdl_driver.c ../bindings/lua/fxtk_lua.c \
                   test/lua_host_sdl.c $(LUA_CORE)
	gcc $(CFLAGS) $(LUA_FLAGS) -DFXTK_LUA_NO_WINAPI $(INC) -I../bindings/lua $(LUA_INC) $(CORE) ../drivers/fxtk_sdl_driver.c \
	    ../bindings/lua/fxtk_lua.c test/lua_host_sdl.c $(LUA_CORE) -o $@ $(LDFLAGS) $(LIBS)

# 绑定自检：无头跑 demo-main/test/lua_test.lua，断言绑定/回调/绘制链路。
lua-test: test/lua_host_sdl
	SDL_VIDEODRIVER=dummy FXTK_SCRIPT=test/lua_test.lua ./test/lua_host_sdl

# ---- v2.5.6: 增量构建规则 (对象清单已上移到文件顶部) ----
$(BUILD)/core/%.o: ../components/fxtk/%.c $(ALL_CORE_HDRS)
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(INC) -c $< -o $@

$(BUILD)/drv/%.o: ../drivers/%.c $(ALL_CORE_HDRS)
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) -c $< -o $@

$(BUILD)/app/%.o: %.c $(ALL_CORE_HDRS)
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) -c $< -o $@

$(BUILD)/lua/%.o: ../third_party/lua/src/%.c
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(LUA_FLAGS) $(LUA_INC) -c $< -o $@

$(BUILD)/lua/fxtk_lua.o: ../bindings/lua/fxtk_lua.c $(ALL_CORE_HDRS)
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(LUA_FLAGS) $(INC) -I../bindings/lua $(LUA_INC) $(SOKOL_INC) -c $< -o $@

# main_lua.c 需要 Lua 头文件 + bindings 头文件 → 不能走通用 drv 规则, 单独一条
$(BUILD)/lua/main_lua.o: ../drivers/main_lua.c $(ALL_CORE_HDRS)
	@mkdir -p $(@D)
	gcc $(CFLAGS) $(LUA_FLAGS) $(INC) -I../bindings/lua $(LUA_INC) $(SOKOL_INC) -c $< -o $@

sokol: fxtk_sim

# ---- 渲染吞吐基准 (dummy 驱动, 无 vsync) ----
#   SDL_VIDEODRIVER=dummy ./test/bench 1200 8 200
# v2.4.4: 原先这里是 1200 帧 / 480x272 → 只跑到 84 个控件, 与 README 引用的口径不符
# (README 写的是 1080P、2820 控件、102-130fps)。现在默认就跑那个口径; 想要快速冒烟用 bench-fast。
bench: test/bench
	SDL_VIDEODRIVER=dummy ./test/bench 4000 8 500 1920 1080

bench-fast: test/bench
	SDL_VIDEODRIVER=dummy ./test/bench 1200 8 200

test/bench: test/bench.c $(CORE) $(HDRS) ../drivers/fxtk_sdl_driver.c app.c app_desktop.c raymarch.c gpu_raymarch.c
	gcc $(CFLAGS) $(INC) $(CORE) ../drivers/fxtk_sdl_driver.c \
	    app.c app_desktop.c raymarch.c gpu_raymarch.c \
	    test/bench.c -o $@ $(LDFLAGS) $(LIBS)

# ---- v2.5.6: 无头渲染一组画布示例 (CI 用来抓 resize 崩溃) ----
# CI 里原先内联手抄了整份核心源清单, 与 Makefile 的 CORE 漂移过一次 (漏 gesture/par/flex/svg/wm
# 导致符号未定义)。这里从 components/fxtk/ 自动发现 (与 tools/golden.sh 同一做法),
# 新增模块自动带上, 不再需要谁记得改这里。
CANVAS_CORE := $(shell ls ../components/fxtk/fxtk*.c | grep -v 'fxtk_font\.c$$')
.PHONY: headless-canvas
headless-canvas:
	for e in canvas_01_primitives canvas_02_immediate canvas_03_offscreen canvas_04_custom_widget canvas_05_image canvas_06_interact canvas_07_aa; do \
	  gcc -O2 $(INC) test/render_canvas.c ../examples/canvas/$$e.c $(CANVAS_CORE) -o /tmp/rc_$$e -lm || exit 1; \
	  /tmp/rc_$$e 480 272 /tmp/$$e.ppm || { echo "render crash at 480: $$e"; exit 1; }; \
	  /tmp/rc_$$e 200 112 /tmp/$${e}_small.ppm || { echo "render crash at 200: $$e"; exit 1; }; \
	done
	@echo "headless canvas render OK"

# ---- v2.5.6: -Wall -Wextra 告警报告 (非阻断; 复用 CORE 清单避免手抄漂移) ----
.PHONY: warnings-check
warnings-check:
	@set +e; \
	gcc -O2 -Wall -Wextra -o /dev/null $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c app.c app_desktop.c \
	  $(LIBS) 2> /tmp/fxtk_warnings.txt; \
	if [ $$? -ne 0 ]; then echo "❌ Warnings step: compile failed"; tail -30 /tmp/fxtk_warnings.txt; exit 1; fi; \
	n=$$(grep -c 'warning:' /tmp/fxtk_warnings.txt); \
	echo "Total -Wall -Wextra warnings: $$n (advisory only)"

# ---- v2.5.6: 裁剪开关一致性自检 ----
# 起因: autotrim 以前只验证过"全开"编译; 关闭 TAB 时 fxtk_anim_reset 仍裸引用 s_ta_w → 直接编译失败;
# 而 autotrim 的正则 bug 又会把【用到】的控件也判成未使用裁掉 —— 两者叠加让 --autotrim 静默产出空界面。
# 这里 1) 全关所有控件开关仍能编译核心; 2) autotrim 对已知用 button/label/checkbox 的示例不得误裁。
.PHONY: trim-check
trim-check:
	@OFF="-DFXTK_WIDGET_BUTTON=0 -DFXTK_WIDGET_LABEL=0 -DFXTK_WIDGET_GRID=0 -DFXTK_WIDGET_CANVAS=0 \
	  -DFXTK_WIDGET_SLIDER=0 -DFXTK_WIDGET_PROGRESS=0 -DFXTK_WIDGET_CHECKBOX=0 -DFXTK_WIDGET_PANEL=0 \
	  -DFXTK_WIDGET_TAB=0 -DFXTK_WIDGET_IMAGE=0 -DFXTK_WIDGET_TEXTEDIT=0 -DFXTK_WIDGET_LIST=0 -DFXTK_WIDGET_DROP=0"; \
	  for f in fxtk.c fxtk_widgets.c fxtk_extra.c fxtk_draw.c; do \
	    gcc -Os $$OFF $(INC) -c ../components/fxtk/$$f -o /tmp/fxtk_trim_$$f.o || exit 1; \
	  done; \
	  T=$$(../tools/autotrim.sh ../examples/ex01_hello.c); \
	  case "$$T" in *-DFXTK_WIDGET_BUTTON=0*|*-DFXTK_WIDGET_LABEL=0*|*-DFXTK_WIDGET_CHECKBOX=0*) \
	    echo "trim-check FAIL: autotrim 误裁了示例用到的控件: $$T"; exit 1;; esac; \
	  echo "trim-check OK: 全关裁剪下核心可编译, autotrim 不误裁用到的控件"

# ---- 示例 (examples/ex*.c 与 examples/canvas/canvas_*.c) ----
ex%: ../examples/ex%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LDFLAGS) $(LIBS_EX)

canvas%: ../examples/canvas/canvas%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LDFLAGS) $(LIBS_EX)

# v2.5.6: 补全 clean —— 此前漏掉 asan/stub 变体、各模块自检、mw_interactive、增量构建 .o 目录,
# 导致测试二进制残留、build/ 里的旧 .o 继续被增量构建复用。
clean:
	rm -rf $(BUILD)
	rm -f fxtk_sim fxtk_sim_en fxtk_sim_sokol fxtk_sim_sdl fxtk_sim_sdl_en fxtk_sim_dbg fxtk_lua fxtk_lua_runner \
	      test/headless_test test/headless_test_stub test/headless_test_asan test/headless_test_asan_stub \
	      test/bench test/lua_host_sdl test/par_test test/uiscale_test test/gesture_test test/flex_test \
	      test/svg_test test/window_test test/v25_showcase test/v25_evidence test/rt_resize \
	      mw_interactive ex[0-9][0-9]_* canvas_[0-9][0-9]_* dist/win/*.exe fxtk_win.exe

# ---- v2.4 金图回归 (无头, 可进 CI) ----
golden:
	../tools/golden.sh
golden-update:
	../tools/golden.sh --update
.PHONY: golden golden-update

# ---- 动态库打包 (exe 只留 app 层) ----
shared:
	./build_shared.sh
.PHONY: shared

# ---- 打发布包 ----
package:
	../tools/package_release.sh
.PHONY: package

# ---- 取证/调试构建: 探针编进二进制, 不依赖环境变量 ----
fxtk_sim_dbg: $(SOKOL_CORE) ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c app.c app_desktop.c raymarch.c gpu_raymarch_stub.c
	gcc -O0 -g -DFXTK_DEBUG_LOG=1 -DFXTK_DEBUG_LOG_DRV=1 $(INC) $(SOKOL_INC) $(SOKOL_CORE) \
	    ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c app.c app_desktop.c raymarch.c \
	    gpu_raymarch_stub.c -o $@ $(SOKOL_LIBS)

.PHONY: dbg

# ---- v2.5: 新功能可视化演示 (真实渲染管线导 PNG, 不需要真窗口) ----
.PHONY: v25-showcase
v25-showcase: test/v25_showcase
	./test/v25_showcase 480 272 /tmp/v25_480.png
	./test/v25_showcase 800 350 /tmp/v25_800.png

test/v25_showcase: test/v25_showcase.c test/render_canvas.c $(HDRS)
	gcc -O2 $(INC) test/render_canvas.c test/v25_showcase.c \
	    $(shell ls ../components/fxtk/fxtk*.c | grep -v 'fxtk_font\.c$$') \
	    -o $@ -lm -lpthread

# ---- v2.5: 无头证据收集 (并行/手势/高DPI/flex/SVG 的可验证数字) ----
.PHONY: v25-evidence
v25-evidence: test/v25_evidence
	./test/v25_evidence

test/v25_evidence: test/v25_evidence.c $(HDRS)
	gcc -O2 $(INC) test/v25_evidence.c \
	    $(shell ls ../components/fxtk/fxtk*.c | grep -v 'fxtk_font\.c$$') \
	    -o $@ -lm -lpthread
