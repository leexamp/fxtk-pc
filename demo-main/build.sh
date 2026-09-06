#!/bin/bash
echo "🔨 Compiling fxtk PC App (v2.2 GPU)..."
LOG=/tmp/fxtk_build.log
gcc -O2 -s -pthread \
    -I. -I../components/fxtk \
    ../components/fxtk/fxtk.c \
    ../components/fxtk/fxtk_draw.c \
    ../components/fxtk/fxtk_widgets.c \
    ../components/fxtk/fxtk_font.c \
    ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c \
    ../components/fxtk/fxtk_fs.c \
    fxtk_sdl_driver.c \
    fxtk_image_sdl.c \
    raymarch.c \
    gpu_raymarch.c \
    main_linux.c \
    app.c \
    app_desktop.c \
    -o fxtk_sim \
    -lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lEGL -lGLESv2 -lX11 2> $LOG
if [ $? -eq 0 ]; then
    echo "✅ Build successful! Running app..."
    ./fxtk_sim
else
    echo "❌ 编译失败 —— 智能提示："
    grep -o "implicit declaration of function '[A-Za-z0-9_]*'" $LOG | sort -u | while read -r l; do
        fn=${l##*\'}; fn=${fn%\'*}
        case "$fn" in
          on_*)     echo "  💡 回调 $fn 未前向声明 → 文件顶部加: static void $fn(fx_widget_t *w, void *ud);" ;;
          fx_*|fxtk_*) echo "  💡 API $fn 未声明 → 在 fxtk.h 补原型, 或确认拼写/是否新版本新增" ;;
          sdl_*)    echo "  💡 驱动函数 $fn 缺失 → 检查 fxtk_sdl_driver.c 是否实现并导出" ;;
          *)        echo "  💡 函数 $fn 未声明 → 补前向声明或 #include 对应头文件" ;;
        esac
    done
    grep -o "undefined reference to '[A-Za-z0-9_]*'" $LOG | sort -u | while read -r l; do
        fn=${l##*\'}; fn=${fn%\'*}
        echo "  💡 链接期 $fn 无定义 → 定义在别处? 加入 build.sh 编译列表, 或声明/定义签名保持一致"
    done
    grep -o "has no member named '[A-Za-z0-9_]*'" $LOG | sort -u | while read -r l; do
        m=${l##*\'}; m=${m%\'*}
        echo "  💡 结构体无成员 $m → 对照 fxtk.h 版本(SDL2 用 tex_coord, SDL3 才叫 uv 等)"
    done
    echo "---- 原始错误(前15行) ----"; head -15 $LOG
fi
