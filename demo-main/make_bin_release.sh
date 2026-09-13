#!/bin/bash
# fxtk v2.4 二进制发布: 编译 Linux demo + Windows 交叉, 打包到 dist/fxtk-v2.2-bin/
VER=v2.4
BIN=dist/fxtk-$VER-bin
rm -rf "$BIN" dist/fxtk-$VER-bin-*
mkdir -p "$BIN/linux" "$BIN/windows"

echo "🔨 编译 Linux demo (fxtk_sim)..."
make -s fxtk_sim 2>/dev/null || { echo "❌ Linux 编译失败"; exit 1; }
make -s fxtk_sim_en 2>/dev/null || { echo "❌ Linux EN 编译失败"; exit 1; }
cp fxtk_sim "$BIN/linux/fxtk_sim-x86_64"
cp fxtk_sim_en "$BIN/linux/fxtk_sim_en-x86_64"
# Linux 二进制剥调试符号 (可执行体积 170KB→146KB)
[ -x "$(command -v strip)" ] && strip "$BIN/linux/fxtk_sim-x86_64" "$BIN/linux/fxtk_sim_en-x86_64" 2>/dev/null

echo "🔨 Windows 交叉编译 (fxtk_win.exe)..."
./build_win_cross.sh app >/dev/null 2>&1 || { echo "❌ Windows 交叉失败"; exit 1; }
./build_win_cross.sh app_en >/dev/null 2>&1 || { echo "❌ Windows EN 交叉失败"; exit 1; }
cp dist/win/fxtk_win.exe "$BIN/windows/"
cp dist/win/fxtk_win_en.exe "$BIN/windows/"
cp dist/win/*.dll "$BIN/windows/" 2>/dev/null
# vendored SDL2_ttf.dll 是带调试符号/断言的构建(68MB), SDL2_image 也含调试段; 剥符号后缩到 ~2MB/0.2MB
[ -x "$(command -v x86_64-w64-mingw32-strip)" ] && \
  for f in "$BIN"/windows/*.dll; do x86_64-w64-mingw32-strip "$f" 2>/dev/null; done
# 同时也剥 exe 的调试符号
[ -x "$(command -v x86_64-w64-mingw32-strip)" ] && \
  for f in "$BIN"/windows/*.exe; do x86_64-w64-mingw32-strip "$f" 2>/dev/null; done

cat > "$BIN/README.txt" <<EOF
fxtk $VER 二进制包 (已剥调试符号, 体积最小化)
- linux/  : fxtk_sim-x86_64 (中文) / fxtk_sim_en-x86_64 (英文)  (PC 模拟器; 需 SDL2/SDL2_ttf/SDL2_image/EGL/GLES2/X11)
- windows/: fxtk_win.exe (中文) / fxtk_win_en.exe (英文) + SDL2*.dll  (64 位, 直接双击)
- 源码: 用 demo-main/make_release.sh 生成 fxtk-$VER.tar.gz
EOF

tar --use-compress-program="gzip -9" -cf "dist/fxtk-$VER-bin-linux.tar.gz" -C "$BIN" linux
( cd "$BIN" && zip -9qr "../fxtk-$VER-bin-windows.zip" windows )
echo "✅ 二进制包已生成:"
ls -la dist/fxtk-$VER-bin-* | awk '{print "  ", $NF, $5" bytes"}'
