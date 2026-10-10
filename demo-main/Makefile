# fxtk 统一构建入口 (v2.3.1 恢复)
#
#   make              # 构建完整演示 fxtk_sim (12 页)
#   make fxtk_sim_en  # 英文演示 (侧边栏标签)
#   make test         # 构建并运行无头单元测试 (无需 SDL/窗口/字体)
#   make ex01_hello   # 构建指定示例: examples/ 下任一 ex*.c
#   make canvas_01_primitives   # 构建画布教程: examples/canvas/ 下任一 canvas_*.c
#   make clean
#
# 注意: `test` 目标显式声明了 .PHONY 且有真实规则 ——
#       避免再次踩 "同名 test/ 目录被 make 当作已是最新" 的假成功坑。
.PHONY: all test clean fxtk_sim fxtk_sim_en fxtk_sim_sokol fxtk_sim_sdl fxtk_sim_sdl_en bench sokol
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
        ../components/fxtk/fxtk_gesture.c
HDRS := ../components/fxtk/fxtk.h ../components/fxtk/fxtk_internal.h
DRIVER := ../drivers/fxtk_sdl_driver.c ../drivers/main_linux.c

# ---- v2.4 起【默认后端 = sokol】----
# SDL2 降为遗留: 只保留 SDL 驱动作为【无头测试/基准/金图】用的假驱动 (SDL_VIDEODRIVER=dummy),
# 因为 CI 里没有显示器, 而 sokol 需要真实 GL 上下文。发布用的演示程序一律走 sokol。
all: fxtk_sim

# 旧名字继续可用(脚本/文档里引用的是它), 现在只是别名 —— 产物仍是 ./fxtk_sim(sokol 版)
fxtk_sim_sokol: fxtk_sim

fxtk_sim: $(SOKOL_CORE) $(WIN_SRC) ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c \
          app.c app_desktop.c raymarch.c gpu_raymarch_stub.c
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) $(SOKOL_CORE) $(WIN_SRC) \
	    ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c app.c app_desktop.c raymarch.c \
	    gpu_raymarch_stub.c -o $@ $(LDFLAGS) $(SOKOL_LIBS)

fxtk_sim_en: $(SOKOL_CORE) $(WIN_SRC) ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c \
             app_en.c app_desktop_en.c raymarch.c gpu_raymarch_stub.c
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) $(SOKOL_CORE) $(WIN_SRC) \
	    ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c app_en.c app_desktop_en.c raymarch.c \
	    gpu_raymarch_stub.c -o $@ $(LDFLAGS) $(SOKOL_LIBS)

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

# ---- v2.4 sokol 后端 (无 SDL 依赖; 文本层走 stb_truetype) ----
SOKOL_INC := -I../third_party/sokol
SOKOL_LIBS := -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
SOKOL_CORE := ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
              ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
              ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c \
              ../components/fxtk/fxtk_par.c ../components/fxtk/fxtk_gesture.c
# ⚠️ 这份清单【不是】CORE 的拷贝, 而是独立维护的(去掉 fxtk_font.c, 改用
#    fxtk_font_stb.c)。新增核心 .c 时【两处都要加】—— v2.5 初版只往 CORE 里加了
#    fxtk_gesture.c, 结果 sokol 版演示报 "fx_gesture_feed 未定义的引用"。

# ---- v2.5: 单窗口桌面行为 (去标题栏/置顶/移动/居中/拖动) ----
# 独立成文件而不是塞进驱动: 它只用 sokol_app 暴露的原生窗口句柄(sapp_x11_get_window
# 等), 完全不碰渲染 —— 所以与帧流程、命令流、金图都无关, 加进来不影响既有行为。
WIN_SRC := ../drivers/fxtk_window.c

# 单窗口行为自检。
# 【刻意不放进 test 目标】: 它要开真窗口并依赖窗口管理器, 不适合当无头 CI 的必过项。
# 无头/Xvfb 下它会如实报 [N/A]/[ENV] 而不是假绿 —— 这正是本项目要守住的原则。
.PHONY: window-test
window-test: test/window_test
	./test/window_test

test/window_test: test/window_test.c test/sokol_app_impl.c $(WIN_SRC) $(HDRS)
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) test/window_test.c test/sokol_app_impl.c \
	    $(WIN_SRC) -o $@ $(LDFLAGS) $(SOKOL_LIBS)

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

# ---- 示例 (examples/ex*.c 与 examples/canvas/canvas_*.c) ----
ex%: ../examples/ex%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LDFLAGS) $(LIBS_EX)

canvas%: ../examples/canvas/canvas%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LDFLAGS) $(LIBS_EX)

clean:
	rm -f fxtk_sim fxtk_sim_en fxtk_sim_sokol test/headless_test test/headless_test_stub test/bench \
	      ex[0-9][0-9]_* canvas_[0-9][0-9]_* dist/win/*.exe fxtk_win.exe

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

# ---- ESP32 侧接口级编译冒烟 ----
esp32-smoke:
	../tools/esp32_smoke.sh
.PHONY: esp32-smoke

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
