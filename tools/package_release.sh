#!/bin/bash
# ============================================================
# package_release.sh — 打发布包 (源码 + 三种产物形态)
#
#   ./tools/package_release.sh [版本号]      默认取 fxtk.h 里的 FXTK_VERSION
#
# 产出 (dist/pkg/):
#   fxtk-<ver>-src.tar.gz            纯源码包(git archive, 无 .git/无构建产物) → 传 GitHub/发布
#   fxtk-<ver>-linux-x86_64.tar.gz   sokol 单文件 + 文档 + 展示图
#   fxtk-<ver>-linux-shared.tar.gz   exe + libfxtk.so + libfxtk_sokol.so (整包可移动, rpath=$ORIGIN)
#   fxtk-<ver>-win-x86_64.zip        Windows 单 exe(无任何第三方 DLL)
#
# 前置: 先构建产物 —— make -C demo-main (Linux) / demo-main/build_win_sokol.sh / build_shared.sh
# ============================================================
set -eu
cd "$(dirname "$0")/.."
VER="${1:-$(grep -oP '#define FXTK_VERSION\s+"\K[^"]+' components/fxtk/fxtk.h | head -1)}"
OUT=dist/pkg
[ -x demo-main/fxtk_sim ] || { echo "缺 demo-main/fxtk_sim, 先跑 make -C demo-main"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/linux" "$OUT/win" "$OUT/shared"

echo "📦 源码包 (git archive)"
git archive --format=tar.gz --prefix="fxtk-$VER/" -o "$OUT/fxtk-$VER-src.tar.gz" HEAD

echo "📦 Linux 单文件"
cp demo-main/fxtk_sim "$OUT/linux/"
cp README.md LICENSE graph.png image.png rending.png texting.png "$OUT/linux/" 2>/dev/null || true
printf 'fxtk %s · Linux x86_64 (sokol 单文件版)\n\n运行: ./fxtk_sim   (无第三方 .so 依赖)\n依赖: 系统 OpenGL(X11) + 中文字体(缺失时自动回退)\n' "$VER" > "$OUT/linux/运行说明.txt"
tar czf "$OUT/fxtk-$VER-linux-x86_64.tar.gz" -C "$OUT/linux" .

echo "📦 Linux 动态库包"
if ls demo-main/dist/shared/*.so >/dev/null 2>&1; then
    cp demo-main/dist/shared/* "$OUT/shared/"
    tar czf "$OUT/fxtk-$VER-linux-shared.tar.gz" -C "$OUT/shared" .
else
    echo "   ⚠ 跳过(先跑 demo-main/build_shared.sh)"
fi

echo "📦 Windows 单 exe"
if [ -f demo-main/dist/win_sokol/fxtk_sokol.exe ]; then
    cp demo-main/dist/win_sokol/fxtk_sokol.exe "$OUT/win/"; cp README.md "$OUT/win/" 2>/dev/null || true
    printf 'fxtk %s · Windows x86_64 (sokol 单 exe)\n\n双击 fxtk_sokol.exe 即可, 无需任何 DLL。\n' "$VER" > "$OUT/win/运行说明.txt"
    python3 -c "
import zipfile,os,sys
ver='$VER'; out='$OUT'; d=out+'/win'
z=zipfile.ZipFile(out+'/fxtk-'+ver+'-win-x86_64.zip','w',zipfile.ZIP_DEFLATED)
for f in os.listdir(d): z.write(os.path.join(d,f), f)
z.close()"
else
    echo "   ⚠ 跳过(先跑 demo-main/build_win_sokol.sh)"
fi

echo "✅ 发布包 ($VER):"
ls -la --block-size=K "$OUT"/*.tar.gz "$OUT"/*.zip 2>/dev/null | awk '{printf "   %-40s %6s KB\n", $9, $5}'
