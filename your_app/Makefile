# 构建定义复用 demo-main/Makefile.common（源清单只维护一处）
include ../demo-main/Makefile.common

# ============================================================
# your_app/Makefile —— 最小起步工程
#   make        构建 ./your_app
#   make run    构建并运行
#   make clean
# 只需要两样东西：框架核心(components/fxtk) + 你要用的驱动(drivers/)
# ============================================================
       ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
       ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c
DRV  := ../drivers/fxtk_app_sokol.c ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c
INC  := -I. -I../components/fxtk -I../third_party/sokol -I../drivers
CFLAGS = -Os -s -flto -ffunction-sections -fdata-sections -DNDEBUG
LIBS   := $(SOKOL_LIBS)

all: your_app
your_app: main.c $(CORE) $(DRV)
	gcc $(CFLAGS) $(INC) main.c $(DRV) $(CORE) -o $@ $(LIBS)
run: your_app
	./your_app
clean:
	rm -f your_app
.PHONY: all run clean
