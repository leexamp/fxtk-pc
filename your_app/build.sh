#!/bin/bash
# ============================================================
# build.sh — your_app 一键构建/运行
#
#   ./build.sh                构建并运行(默认 -Os)
#   ./build.sh --no-run       只构建
#   ./build.sh -O2            换优化级别(透传给 CFLAGS)
#   ./build.sh --autotrim     先扫描 main.c 真正用到的控件, 生成裁剪开关再构建(体积更小)
#   ./build.sh --shared       动态库形态: exe 只留应用层, 框架编成 libfxtk.so
#   ./build.sh --release      构建 + 打包到 dist/(含运行说明)
#   ./build.sh --clean        清理产物
#
# 为什么要 --autotrim: 框架支持 -DFXTK_WIDGET_XXX=0 在编译期裁掉用不到的控件。
# 手写这堆开关很烦, 而且以后控件改名字容易漏 → 这里直接扫源码生成, 避免"功能加多了编不过/装不下"。
# ============================================================
set -e
cd "$(dirname "$0")"
AUTOTRIM=0; MODE=run; OPT=""
for a in "$@"; do
  case "$a" in
    --no-run)   MODE=build ;;
    --release)  MODE=release ;;
    --shared)   MODE=shared ;;
    --autotrim) AUTOTRIM=1 ;;
    --clean)    MODE=clean ;;
    -O0|-O1|-O2|-O3|-Os|-Oz) OPT="$a" ;;
    *) echo "未知参数: $a (见脚本头部用法)"; exit 2 ;;
  esac
done

BASE="-s -flto -ffunction-sections -fdata-sections -DNDEBUG"
[ -n "$OPT" ] && BASE="$OPT $BASE"
[ -z "$OPT" ] && BASE="-Os $BASE"

if [ "$AUTOTRIM" = 1 ]; then
    TRIM="$(../tools/autotrim.sh main.c)"
    echo "🔍 autotrim 裁剪开关: $(echo $TRIM | wc -w) 项"
    MAKE_CFLAGS="$BASE $TRIM"
else
    MAKE_CFLAGS="$BASE"
fi

case "$MODE" in
  clean)  rm -f your_app libfxtk.so; rm -rf dist; echo "已清理"; exit 0 ;;
  build)  rm -f your_app; make CFLAGS="$MAKE_CFLAGS" >/dev/null; ls -la --block-size=K your_app | awk '{print "✅ 构建完成: your_app ("$5")"}'; exit 0 ;;
  run)    rm -f your_app; make CFLAGS="$MAKE_CFLAGS" >/dev/null; exec ./your_app ;;
esac

if [ "$MODE" = shared ]; then
    # 动态库形态(与 demo-main/build_shared.sh 同一分法):
    #   libfxtk.so        = 框架核心
    #   libfxtk_sokol.so  = sokol 驱动 + stb 文本层(★ SOKOL_*_IMPL 在这里, 所以 sokol 只编进 .so)
    #   your_app          = 只含 main.c 与外壳(入口), 不再包含 sokol 实现
    # 上一版我误把驱动编进 exe → 整套 sokol 进了可执行文件(141K); 正确分法下 exe 只有几十 KB。
    rm -f your_app libfxtk.so libfxtk_sokol.so
    CORE_CS="../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c              ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c"
    echo "🔨 libfxtk.so (框架核心) ..."
    gcc -shared -fPIC -Os -s -flto -ffunction-sections -fdata-sections -DNDEBUG \
        -I../components/fxtk -I../drivers -I../third_party/sokol \
        $CORE_CS -o libfxtk.so -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
    echo "🔨 libfxtk_sokol.so (sokol 驱动 + 文本层, 含 sokol 实现) ..."
    gcc -shared -fPIC -Os -s -flto -ffunction-sections -fdata-sections -DNDEBUG \
        -I../components/fxtk -I../drivers -I../third_party/sokol \
        ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c \
        -o libfxtk_sokol.so -L. -lfxtk -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
    echo "🔨 your_app (只含应用 + 外壳入口) ..."
    gcc $MAKE_CFLAGS -I. -I../components/fxtk -I../drivers -I../third_party/sokol \
        main.c ../drivers/fxtk_app_sokol.c \
        -o your_app -L. -lfxtk_sokol -lfxtk -Wl,-rpath,'$ORIGIN' -Wl,--export-dynamic \
        -lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm
    ls -la --block-size=K libfxtk.so libfxtk_sokol.so your_app | awk '{print "  "$5"  "$9}'
    echo "✅ 动态库形态完成(对照: demo 的 fxtk_shared 约 60KB)"; exit 0
fi

if [ "$MODE" = release ]; then
    rm -f your_app; make CFLAGS="$MAKE_CFLAGS" >/dev/null
    rm -rf dist && mkdir -p dist && cp your_app dist/
    cp ../README.md dist/ 2>/dev/null || true
    printf 'your_app —— fxtk 起步示例\n\n运行: ./your_app\n依赖: 系统 OpenGL / X11(无第三方动态库)\n' > dist/运行说明.txt
    echo "✅ 已打包到 your_app/dist/"; ls dist; exit 0
fi
