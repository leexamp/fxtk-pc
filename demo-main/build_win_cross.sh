#!/bin/bash
# ============================================================
# Windows 交叉编译 · 开发循环版 (产物直落共享文件夹 dist/win)
#   ./build_win_cross.sh              -> dist/win/fxtk_win.exe        (完整演示)
#   ./build_win_cross.sh ex07_snake   -> dist/win/ex07_snake_win.exe  (任意示例)
#   ./build_win_cross.sh app --zip    -> 顺便打发布 zip
# 虚拟机里 Z: 盘直接双击 exe, 无需拷贝/解压!
# ============================================================
TARGET=${1:-app}; MODE=$2
# ---- 依赖自举: 编译器或 vendored SDL 缺一即自动补 (v2.3: 不再直接失败) ----
if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || [ ! -d third_party/SDL2-win/SDL2-2.30.10 ]; then
    echo "[setup] 交叉编译依赖不全, 自动执行 setup_win_cross.sh ..."
    ./setup_win_cross.sh
fi
W=third_party/SDL2-win
S2=$W/SDL2-2.30.10/x86_64-w64-mingw32
TT=$W/SDL2_ttf-2.22.0/x86_64-w64-mingw32
IM=$W/SDL2_image-2.8.2/x86_64-w64-mingw32
INC="-I$S2/include -I$S2/include/SDL2 -I$TT/include -I$TT/include/SDL2 -I$IM/include -I$IM/include/SDL2"
LIB="-L$S2/lib -L$TT/lib -L$IM/lib"
mkdir -p dist/win
if [ "$TARGET" = "app" ]; then
    SRCS="app.c app_desktop.c raymarch.c gpu_stub_win.c"; OUT=fxtk_win.exe
elif [ "$TARGET" = "app_en" ]; then
    SRCS="app_en.c app_desktop_en.c raymarch.c gpu_stub_win.c"; OUT=fxtk_win_en.exe
else
    EXDIR=examples; [ -d "$EXDIR" ] || EXDIR=../examples
    SRCS="$EXDIR/$TARGET.c"; OUT=${TARGET}_win.exe
fi
[ -f gpu_stub_win.c ] || { echo "⚠️ 缺 gpu_stub_win.c, 先跑 fix_win_final.py"; exit 1; }
echo "🔨 [$TARGET] -> dist/win/$OUT"
# v2.3: 体积优化档 (与 Makefile 同步: -Os+LTO+gc-sections+去unwind, 801KB → 182KB, -77%)
# ⚠ mingw 实测: -fdata-sections 与 LTO 同用会让 PE 的 .data 实体化 ~44.6MB 零填充
#   (ELF 无此问题), 故 Windows 侧必须去掉它 —— 别"顺手加回来"。
SZ="-Os -s -flto -ffunction-sections \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-stack-protector -fno-ident \
    -Wl,--gc-sections -Wl,--build-id=none"
x86_64-w64-mingw32-gcc $SZ -I. -I../components/fxtk $INC \
    ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
    ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_font.c \
    ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c \
    ../components/fxtk/fxtk_fs.c \
    fxtk_sdl_driver.c fxtk_image_sdl.c main_linux.c $SRCS \
    -o dist/win/$OUT $LIB \
    -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lSDL2_image -lshell32 -lole32 -lm \
    -static-libgcc -Wl,-Bstatic -lpthread -Wl,-Bdynamic || { echo "❌ 编译失败"; exit 1; }
# DLL 只在首次拷 (共享文件夹, 一次管永远)
if [ ! -f dist/win/SDL2.dll ]; then
    for d in "$S2/bin" "$TT/bin" "$IM/bin"; do cp -u "$d"/*.dll dist/win/ 2>/dev/null; done
fi
[ "$MODE" = "--zip" ] && { (cd dist && rm -f fxtk-v2.3-windows.zip && zip -qr fxtk-v2.3-windows.zip win); echo "📦 zip 已更新"; }
echo "✅ 完成: 虚拟机双击 dist/win/$OUT 即玩"

# 冒烟: ./build_win_cross.sh app --wine
if [ "$MODE" = "--wine" ]; then
    echo "🍷 wine 冒烟测试 (关窗口结束)..."
    (cd dist/win && wine ./$OUT)
fi
