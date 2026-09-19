#!/bin/bash
# ============================================================
# setup_win_cross.sh — Windows 交叉编译依赖自举 (幂等)
#   工具链/vendored SDL 已就绪则秒退; 只在真缺时才动 sudo。
#   用法: cd demo-main && ./setup_win_cross.sh
#   (build_win_cross.sh 检测到依赖缺失时会自动调用本脚本)
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"

# 1) mingw 交叉工具链: 已有则跳过, 避免无谓 sudo
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "[ok] mingw 工具链: gcc $(x86_64-w64-mingw32-gcc -dumpversion)"
else
    echo "[setup] 安装 mingw-w64 工具链 (需要 sudo)..."
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update -qq
        sudo apt-get install -y gcc-mingw-w64-x86-64 wget curl zip
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm mingw-w64-gcc wget curl zip
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y mingw64-gcc wget curl zip
    else
        echo "❌ 未识别的包管理器, 请手动安装 gcc-mingw-w64-x86-64"; exit 1
    fi
fi

# 2) vendored SDL2 Windows 开发包 (mingw 版, 解压即用, URL 已验证 200)
W=third_party/SDL2-win
V=2.30.10; T=2.22.0; I=2.8.2
mkdir -p "$W" && cd "$W"

fetch() {  # fetch <url> <tarball> <解压后期望目录>
    local url=$1 tb=$2 dir=$3
    [ -d "$dir" ] && { echo "[ok] $dir 已就绪"; return 0; }
    echo "[setup] 下载 $dir ..."
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 3 --retry-delay 2 -o "$tb" "$url"
    else
        wget -q --tries=3 -O "$tb" "$url"
    fi
    tar xf "$tb"
    [ -d "$dir" ] || { echo "❌ 解压后缺少 $dir (GitHub 资产名可能变了, 请核对 release 页)"; exit 1; }
}

fetch "https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-devel-$V-mingw.tar.gz" \
      "SDL2-devel-$V-mingw.tar.gz" "SDL2-$V"
fetch "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$T/SDL2_ttf-devel-$T-mingw.tar.gz" \
      "SDL2_ttf-devel-$T-mingw.tar.gz" "SDL2_ttf-$T"
fetch "https://github.com/libsdl-org/SDL_image/releases/download/release-$I/SDL2_image-devel-$I-mingw.tar.gz" \
      "SDL2_image-devel-$I-mingw.tar.gz" "SDL2_image-$I"

echo "✅ Windows 交叉编译依赖就绪 (third_party/SDL2-win)"
