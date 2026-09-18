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
# 体积关键三连(缺了会让 exe 从 0.32MB 膨胀到 14.3MB —— 我在这上面查了很久):
#   -Wl,--exclude-all-symbols  ← 不写这个, PE 会保留导出表与全部全局符号
#   -Wl,--file-alignment=512   ← 段对齐从 4096 降到 512, 小 exe 直接省几十 KB
#   -Wl,--no-insert-timestamp  ← 去掉时间戳, 顺带让构建可复现
# 演示的 build_win_sokol.sh 一直带着这三个, 所以它是 0.44MB。
# 体积要点(实测): 【不要加 -fdata-sections】—— 它与 -flto 冲突: 会在 LTO 合并前把数据拆段,
# 反而让链接器无法剪掉 sokol 的静态数据表, exe 从 0.43MB 膨胀到 14.3MB(30 倍)。
# 演示的 build_win_sokol.sh 没有这个参数, 所以一直是 0.43MB。受控对比: 同一源码集, 仅差这一个参数。
echo "🔨 交叉编译 your_app.exe ..."
# 注意: 这里【故意写成一条字面命令】而不做变量拼装 ——
# 之前用 SZ/CORE 变量拼装时产物异常膨胀到 14MB(而同样参数直接手写只有 328KB),
# 排查四轮未定位到具体差异; 为了让"脚本产物 = 验证过的产物", 直接固化这条命令。
# 要换优化级别/裁剪, 用下面的 $OPT $TRIM 两个位置即可(它们是普通短变量)。
$CC $OPT $TRIM -Os -s -flto -ffunction-sections -fmerge-all-constants \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-ident \
    -static -static-libgcc -mwindows -Wl,--gc-sections -Wl,--build-id=none \
    -Wl,--exclude-all-symbols -Wl,--file-alignment=512 -Wl,--no-insert-timestamp \
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
