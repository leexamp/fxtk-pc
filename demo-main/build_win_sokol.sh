#!/bin/bash
# ============================================================
# Windows 交叉编译 · sokol 后端 (单 exe, 无第三方 DLL)
#   ./build_win_sokol.sh               -> dist/win_sokol/fxtk_sokol.exe
#   ./build_win_sokol.sh app_en        -> 英文版
#   ./build_win_sokol.sh --autotrim    -> 按 app.c 实际用到的控件裁剪后再编(体积更小)
#   ./build_win_sokol.sh app_en --autotrim
# 与 SDL 版的关键差别: 不链接 SDL2/SDL2_ttf, 只链系统 opengl32 —— 产物是单个 exe,
# 不需要随包发 DLL(这是路线图 v2.4 "单 exe 无 DLL 依赖" 的达标路径)。
# ============================================================
set -e
cd "$(dirname "$0")"

# v2.4.5 修复: 原先只读 $1 当 TARGET, 从不解析开关。
# CI 用 `build_win_sokol.sh --autotrim` 调用时, "--autotrim" 会被当成目标名,
# 落到下面的 else 分支 → 【静默编出英文版】, 而裁剪一次都没生效
# (CI 那步的注释还写着"顺带验控件裁剪开关在 Windows 路径也成立", 实际验了个寂寞)。
# 现在先把开关挑出来, 剩下的非选项参数才当目标名。
TARGET=app; AUTOTRIM=0
for a in "$@"; do
  case "$a" in
    --autotrim) AUTOTRIM=1 ;;
    -*) echo "未知参数: $a (支持: [app|app_en] [--autotrim])" >&2; exit 2 ;;
    *)  TARGET="$a" ;;
  esac
done

command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || { echo "缺 mingw, 先跑 ./setup_win_cross.sh"; exit 1; }
if [ "$TARGET" = "app" ]; then SRCS_APP="app.c app_desktop.c"; OUT=fxtk_sokol.exe
elif [ "$TARGET" = "app_en" ]; then SRCS_APP="app_en.c app_desktop_en.c"; OUT=fxtk_sokol_en.exe
else echo "未知目标: $TARGET (支持 app / app_en)" >&2; exit 2; fi

TRIM=""
if [ "$AUTOTRIM" = 1 ]; then
  TRIM="$(../tools/autotrim.sh $SRCS_APP)"
  echo "🔍 autotrim 裁剪开关: $(echo $TRIM | wc -w) 项"
fi

mkdir -p dist/win_sokol
# 体积: 与 Linux 侧同档, 但按 mingw 实测【不能】加 -fdata-sections(LTO 会把 PE 的 .data
# 实体化成几十 MB 零填充), 见 build_win_cross.sh 里的同一注释。
# -static/-static-libgcc: 把 winpthreads 与 libgcc 静态链进去 —— 否则 raymarch 用的线程
# 会引出 libwinpthread-1.dll, 那就又变成"要带 DLL"了(与达标目标相反)。
SZ="-Os -s -flto -ffunction-sections -static -static-libgcc -fmerge-all-constants \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-stack-protector -fno-ident \
    -Wl,--gc-sections -Wl,--build-id=none"
echo "🔨 [sokol/$TARGET] -> dist/win_sokol/$OUT"
x86_64-w64-mingw32-gcc $SZ $TRIM -I. -I../components/fxtk -I../drivers -I../third_party/sokol \
    ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
    ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
    ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c \
    ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c ../drivers/main_sokol.c \
    $SRCS_APP raymarch.c gpu_raymarch_stub.c \
    -o dist/win_sokol/$OUT \
    -mwindows -Wl,--exclude-all-symbols -Wl,--file-alignment=512 -Wl,--no-insert-timestamp \
    -lopengl32 -lgdi32 -luser32 -lshell32 -lole32 -lcomdlg32 -ldwmapi -lpthread -lm
ls -la dist/win_sokol/$OUT | awk '{printf "✅ 产物: %s  (%.1f KB)\n", $9, $5/1024}'
echo "依赖(应只有 Windows 系统 DLL):"
x86_64-w64-mingw32-objdump -p dist/win_sokol/$OUT | grep "DLL Name" | sort -u | sed 's/^/   /'
