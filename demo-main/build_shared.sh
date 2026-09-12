#!/bin/bash
# ============================================================
# build_shared.sh — 把 fxtk 核心与 sokol 后端打成动态库, exe 只留 app 层
#
#   ./build_shared.sh                -> dist/shared/{fxtk_shared, libfxtk.so, libfxtk_sokol.so}
#   ./build_shared.sh app_en         -> 英文 demo
#
# 为什么能显著减小 exe: 框架核心(6 个 .c)与 sokol 平台层(驱动 + stb 文本)都是
#   后端无关/纯实现, 与 app 层没有交叉依赖 —— 抽成 .so 后多个示例可以共用同一份库,
#   exe 只剩 app 本身。rpath 用 $ORIGIN(而不是绝对路径), 所以整包拷贝到别处也能直接运行。
# ============================================================
set -e
cd "$(dirname "$0")"
TARGET=${1:-app}
if [ "$TARGET" = "app_en" ]; then SRCS_APP="app_en.c app_desktop_en.c"; OUTBIN=fxtk_shared_en; else SRCS_APP="app.c app_desktop.c"; OUTBIN=fxtk_shared; fi
OUT=dist/shared; mkdir -p $OUT
SZ="-Os -s -flto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-ident"
LNK="-Wl,--gc-sections -Wl,--build-id=none"
INC="-I. -I../components/fxtk -I../third_party/sokol"
CORE="../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c \
      ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c"

echo "🔨 libfxtk.so        (核心框架, 与后端无关)"
gcc $SZ -fPIC -shared $INC $CORE -o $OUT/libfxtk.so -lm -lpthread

echo "🔨 libfxtk_sokol.so  (sokol 平台层 + stb 文本; SOKOL_*_IMPL 只在这一个 TU)"
gcc $SZ -fPIC -shared $INC fxtk_sokol_driver.c fxtk_font_stb.c -o $OUT/libfxtk_sokol.so \
    -L$OUT -Wl,-rpath,'$ORIGIN' -lfxtk -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm

echo "🔨 $OUTBIN           (app 层)"
gcc $SZ $LNK $INC main_sokol.c $SRCS_APP raymarch.c gpu_raymarch_stub.c -o $OUT/$OUTBIN \
    -L$OUT -Wl,-rpath,'$ORIGIN' -Wl,--export-dynamic -lfxtk -lfxtk_sokol -lGL -lm -lpthread

cat > $OUT/README.txt <<'TXT'
fxtk v2.4 demo (动态库打包版)
  运行:     ./fxtk_shared          (三个文件要在同一目录; rpath 已设为 $ORIGIN)
  静态版对比: make fxtk_sim_sokol  -> 单个可执行文件(不依赖这两个 .so)
  重新生成: ./build_shared.sh [app|app_en]
TXT
echo "✅ 打包完成:"
ls -la $OUT | awk 'NR>1 && $5>0 {printf "   %-22s %8.1f KB\n", $9, $5/1024}'
