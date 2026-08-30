#!/bin/bash
# 用法:
#   ./build_ex.sh               # 全构建 examples/*.c + examples/canvas/*.c 并逐个运行 (Ctrl+C 跳下一个)
#   ./build_ex.sh ex03_anim     # 只构建并运行指定示例
#   ./build_ex.sh canvas_01_primitives  # 构建 examples/canvas/ 下的画布教程示例
#   ./build_ex.sh all --no-run  # 全构建但不运行
EX=${1:-all}
EXDIR=examples
[ -d "$EXDIR" ] || EXDIR=../examples
CANV="$EXDIR/canvas"
SRCS="../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
      ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_font.c \
      ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c \
      fxtk_sdl_driver.c fxtk_image_sdl.c main_linux.c"
LIBS="-lSDL2 -lSDL2_ttf -lSDL2_image -lm -pthread -lX11"

build_one() {
    local f="$1" out
    out="${f%.c}"
    echo "🔨 Building $f"
    gcc -O2 -s -I. -I../components/fxtk $SRCS "$f" -o "$out" $LIBS
}

if [ "$EX" = "all" ]; then
    FAIL=0; N=0
    for f in "$EXDIR"/ex*.c "$CANV"/*.c; do
        [ -f "$f" ] || continue
        N=$((N+1))
        build_one "$f" || { echo "❌ $f 编译失败"; FAIL=1; continue; }
    done
    echo "================================"
    echo "✅ 全部构建完成: $N 个示例"
    [ "$FAIL" = "0" ] && echo "✅ 全部成功" || echo "⚠️ 有失败 (见上)"
    if [ "$2" != "--no-run" ]; then
        echo "逐个运行 (关闭窗口/Ctrl+C 进入下一个)..."
        for f in "$EXDIR"/ex*.c "$CANV"/*.c; do
            [ -f "$f" ] || continue
            out="${f%.c}"; echo "▶ $out"; "$out" || true
        done
    fi
else
    f="$EXDIR/$EX.c"
    [ -f "$f" ] || f="$CANV/$EX.c"
    if [ -f "$f" ]; then
        build_one "$f" && "${f%.c}" || echo "❌ build failed"
    else
        echo "❌ 未找到示例: $EX (examples/*.c 或 examples/canvas/*.c)"; exit 1
    fi
fi
