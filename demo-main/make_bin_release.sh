#!/bin/bash
# fxtk v2.2 二进制发布: 编译 Linux demo + Windows 交叉, 打包到 dist/fxtk-v2.2-bin/
VER=v2.2
BIN=dist/fxtk-$VER-bin
rm -rf "$BIN" dist/fxtk-$VER-bin-*
mkdir -p "$BIN/linux" "$BIN/windows"

echo "🔨 编译 Linux demo (fxtk_sim)..."
make -s fxtk_sim 2>/dev/null || { echo "❌ Linux 编译失败"; exit 1; }
cp fxtk_sim "$BIN/linux/fxtk_sim-x86_64"

echo "🔨 Windows 交叉编译 (fxtk_win.exe)..."
./build_win_cross.sh app >/dev/null 2>&1 || { echo "❌ Windows 交叉失败"; exit 1; }
cp dist/win/fxtk_win.exe "$BIN/windows/"
cp dist/win/*.dll "$BIN/windows/" 2>/dev/null

cat > "$BIN/README.txt" <<EOF
fxtk $VER 二进制包
- linux/  : fxtk_sim-x86_64  (PC 模拟器; 需 SDL2/SDL2_ttf/SDL2_image/EGL/GLES2/X11)
- windows/: fxtk_win.exe + SDL2*.dll  (64 位, 直接双击)
- 源码: 用 demo-main/make_release.sh 生成 fxtk-$VER.tar.gz
EOF

tar czf "dist/fxtk-$VER-bin-linux.tar.gz" -C "$BIN" linux
( cd "$BIN" && zip -qr "../fxtk-$VER-bin-windows.zip" windows )
echo "✅ 二进制包已生成:"
ls -la dist/fxtk-$VER-bin-* | awk '{print "  ", $NF, $5" bytes"}'
