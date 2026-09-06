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
.PHONY: all test clean fxtk_sim fxtk_sim_en
.DEFAULT_GOAL := fxtk_sim

CFLAGS ?= -O2 -s
INC    := -I. -I../components/fxtk

# demo 链接 (3D 页需要 EGL/GLES/X11)
LIBS    := -lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lEGL -lGLESv2 -lX11
# 示例链接 (不含 GPU 光追, 无需 EGL)
LIBS_EX := -lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lX11

# ---- 核心库单一源清单 (demo / examples / test 共用) ----
# v2.3.1: 补上 fxtk_fs.c (fx_fs_pick_dir/fx_fs_list, app_desktop.c 与 canvas_08 依赖)。
# 此前 build.sh/build_ex.sh/build_win.sh 三处清单都漏了它, 链接必败。
CORE := ../components/fxtk/fxtk.c \
        ../components/fxtk/fxtk_draw.c \
        ../components/fxtk/fxtk_widgets.c \
        ../components/fxtk/fxtk_font.c \
        ../components/fxtk/fxtk_effects.c \
        ../components/fxtk/fxtk_extra.c \
        ../components/fxtk/fxtk_fs.c
HDRS := ../components/fxtk/fxtk.h ../components/fxtk/fxtk_internal.h
DRIVER := fxtk_sdl_driver.c fxtk_image_sdl.c main_linux.c

all: fxtk_sim

fxtk_sim: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app.c app_desktop.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app.c app_desktop.c -o $@ $(LIBS)

fxtk_sim_en: $(CORE) $(HDRS) $(DRIVER) raymarch.c gpu_raymarch.c app_en.c app_desktop_en.c
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) raymarch.c gpu_raymarch.c \
	    app_en.c app_desktop_en.c -o $@ $(LIBS)

# ---- 无头单元测试 ----
# 不链 fxtk_font.c: 测试文件自带文字/纹理桩 (无 SDL/字体依赖)。
test: test/headless_test
	./test/headless_test

TEST_CORE := $(filter-out ../components/fxtk/fxtk_font.c,$(CORE))
test/headless_test: test/headless_test.c $(TEST_CORE) $(HDRS)
	gcc -O2 $(INC) $(TEST_CORE) test/headless_test.c -o $@ -lm

# ---- 示例 (examples/ex*.c 与 examples/canvas/canvas_*.c) ----
ex%: ../examples/ex%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LIBS_EX)

canvas%: ../examples/canvas/canvas%.c $(CORE) $(HDRS) $(DRIVER)
	gcc $(CFLAGS) $(INC) $(CORE) $(DRIVER) $< -o $@ $(LIBS_EX)

clean:
	rm -f fxtk_sim fxtk_sim_en test/headless_test \
	      ex[0-9][0-9]_* canvas_[0-9][0-9]_* dist/win/*.exe fxtk_win.exe
