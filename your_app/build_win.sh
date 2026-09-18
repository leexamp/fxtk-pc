#!/bin/bash
# ============================================================
# build_win.sh — 交叉编译出 Windows 单 exe（在 Linux 上即可完成, 不需要 Windows 机器）
#
#   ./build_win.sh            构建 → dist/win/your_app.exe
#   ./build_win.sh -O2        换优化级别
#   ./build_win.sh --autotrim 先按 main.c 裁控件再构建(体积更小)
#
# 依赖: mingw-w64 交叉编译器(只需编译器, 不需要任何 SDL/其他库)
#   Debian/Ubuntu: sudo apt install gcc-mingw-w64-x86-64
#   MSYS2:         pacman -S mingw-w64-x86_64-gcc
#   仓库也带了一键脚本: ../demo-main/setup_win_cross.sh
#
# 产物是**静态链接**的单文件 exe: 只依赖系统 DLL(opengl32/gdi32/user32/shell32/ole32/comdlg32/dwmapi),
# 没有 SDL2.dll、没有 libwinpthread、没有 libgcc_s —— 拷过去双击即可。
# ============================================================
set -e
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc
command -v $CC >/dev/null 2>&1 || { echo "❌ 缺 $CC。安装: sudo apt install gcc-mingw-w64-x86-64 (或跑 ../demo-main/setup_win_cross.sh)"; exit 1; }

AUTOTRIM=0; OPT=""
for a in "$@"; do
  case "$a" in
    --autotrim) AUTOTRIM=1 ;;
    -O0|-O1|-O2|-O3|-Os|-Oz) OPT="$a" ;;
    *) echo "未知参数: $a"; exit 2 ;;
  esac
done

TRIM=""
if [ "$AUTOTRIM" = 1 ]; then
    TRIM="$(../tools/autotrim.sh main.c)"
    echo "🔍 autotrim: $(echo $TRIM | wc -w) 项裁剪开关"
fi


mkdir -p dist/win
# ⚠️ 已知异常(未解决): 本脚本产出的 exe 约 14MB, 而【同一条命令手工执行】只有 328KB
#    (已用 bash -x 核对命令行完全一致, 手工那次退出码 0、文件为新生成, 尺寸可信)。
#    演示的 build_win_sokol.sh 产出 442KB 属正常。在定位清楚前:
#      · Windows 用户可先用 ../demo-main/build_win_sokol.sh 的流程做对照
#      · 不要仅凭本脚本的产物大小判断"体积已经优化好了"
#    排查建议: 逐段二分(先只编 main.c + 外壳跑通, 再逐步加入 core 各文件), 对比每步产物大小。
echo "🔨 交叉编译 your_app.exe ..."
# 注意: 这里【故意写成一条字面命令】而不做变量拼装 ——
# 之前用 SZ/CORE 变量拼装时产物异常膨胀到 14MB(而同样参数直接手写只有 328KB),
# 排查四轮未定位到具体差异; 为了让"脚本产物 = 验证过的产物", 直接固化这条命令。
# 要换优化级别/裁剪, 用下面的 $OPT $TRIM 两个位置即可(它们是普通短变量)。
$CC $OPT $TRIM -Os -s -flto -ffunction-sections -fdata-sections -fmerge-all-constants \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-ident \
    -static -static-libgcc -mwindows -Wl,--gc-sections -Wl,--build-id=none \
    -I. -I../components/fxtk -I../drivers -I../third_party/sokol \
    main.c ../drivers/fxtk_app_sokol.c ../drivers/fxtk_sokol_driver.c ../drivers/fxtk_font_stb.c \
    ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c \
    ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c \
    -o dist/win/your_app.exe \
    -lopengl32 -lgdi32 -luser32 -lshell32 -lole32 -lcomdlg32 -ldwmapi -lpthread -lm

printf 'your_app —— fxtk 起步示例 (Windows x86_64)\r\n\r\n双击 your_app.exe 运行。\r\n依赖: 仅系统 DLL(OpenGL 等), 无需安装任何运行库。\r\n' > dist/win/运行说明.txt
ls -la --block-size=K dist/win/your_app.exe | awk '{print "✅ 产物: "$5}'
echo "依赖检查(应只见系统 DLL):"
x86_64-w64-mingw32-objdump -p dist/win/your_app.exe 2>/dev/null | grep 'DLL Name' | sed 's/^/   /'
