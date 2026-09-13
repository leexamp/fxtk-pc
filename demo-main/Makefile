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
#   需要 -O2 调试时: make CFLAGS="-O2 -g" LDFLAGS=
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
INC    := -I. -I../components/fxtk

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
        ../components/fxtk/fxtk_backends.c
HDRS := ../components/fxtk/fxtk.h ../components/fxtk/fxtk_internal.h
DRIVER := fxtk_sdl_driver.c main_linux.c

# ---- v2.4 起【默认后端 = sokol】----
# SDL2 降为遗留: 只保留 SDL 驱动作为【无头测试/基准/金图】用的假驱动 (SDL_VIDEODRIVER=dummy),
# 因为 CI 里没有显示器, 而 sokol 需要真实 GL 上下文。发布用的演示程序一律走 sokol。
all: fxtk_sim

# 旧名字继续可用(脚本/文档里引用的是它), 现在只是别名 —— 产物仍是 ./fxtk_sim(sokol 版)
fxtk_sim_sokol: fxtk_sim

fxtk_sim: $(SOKOL_CORE) fxtk_sokol_driver.c fxtk_font_stb.c main_sokol.c \
          app.c app_desktop.c raymarch.c gpu_raymarch_stub.c
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) $(SOKOL_CORE) \
	    fxtk_sokol_driver.c fxtk_font_stb.c main_sokol.c app.c app_desktop.c raymarch.c \
	    gpu_raymarch_stub.c -o $@ $(LDFLAGS) $(SOKOL_LIBS)

fxtk_sim_en: $(SOKOL_CORE) fxtk_sokol_driver.c fxtk_font_stb.c main_sokol.c \
             app_en.c app_desktop_en.c raymarch.c gpu_raymarch_stub.c
	gcc $(CFLAGS) $(INC) $(SOKOL_INC) $(SOKOL_CORE) \
	    fxtk_sokol_driver.c fxtk_font_stb.c main_sokol.c app_en.c app_desktop_en.c raymarch.c \
	    gpu_raymarch_stub.c -o $@ $(LDFLAGS) $(SOKOL_LIBS)

# ---- 遗留: SDL 版演示 (仅用于对照/过渡; 新功能不再往里加) ----
fxtk_sim_sdl: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app.c app_desktop.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app.c app_desktop.c -o $@ $(LDFLAGS) $(LIBS)

fxtk_sim_sdl_en: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app_en.c app_desktop_en.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app_en.c app_desktop_en.c -o $@ $(LDFLAGS) $(LIBS)

# ---- 无头单元测试 ----
# 不链 fxtk_font.c: 测试文件自带文字/纹理桩 (无 SDL/字体依赖)。
# v2.4: 同时跑一份 stub 后端变体 (-DFXTK_BACKEND_STUB: 确定性时钟/定种子随机/内存文件系统),
#       保证测试结果不依赖宿主机的时钟、随机源与文件系统。
test: test/headless_test test/headless_test_stub
	./test/headless_test
	./test/headless_test_stub

TEST_CORE := $(filter-out ../components/fxtk/fxtk_font.c,$(CORE))
test/headless_test: test/headless_test.c $(TEST_CORE) $(HDRS)
	gcc -O2 $(INC) $(TEST_CORE) test/headless_test.c -o $@ -lm

test/headless_test_stub: test/headless_test.c $(TEST_CORE) $(HDRS)
	gcc -O2 -DFXTK_BACKEND_STUB $(INC) $(TEST_CORE) test/headless_test.c -o $@ -lm

# ---- v2.4 sokol 后端 (无 SDL 依赖; 文本层走 stb_truetype) ----
SOKOL_INC := -I../third_party/sokol
SOKOL_LIBS := -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
SOKOL_CORE := ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
              ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
              ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c

sokol: fxtk_sim

# ---- 渲染吞吐基准 (dummy 驱动, 无 vsync) ----
#   SDL_VIDEODRIVER=dummy ./test/bench 1200 8 200
bench: test/bench
	SDL_VIDEODRIVER=dummy ./test/bench 1200 8 200

test/bench: test/bench.c $(CORE) $(HDRS) fxtk_sdl_driver.c app.c app_desktop.c raymarch.c gpu_raymarch.c
	gcc $(CFLAGS) $(INC) $(CORE) fxtk_sdl_driver.c \
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
