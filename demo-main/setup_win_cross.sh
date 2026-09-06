#!/bin/bash
set -e
sudo apt install -y mingw-w64 wget zip
mkdir -p third_party/SDL2-win && cd third_party/SDL2-win
V=2.30.10; T=2.22.0; I=2.8.2
[ -d SDL2-$V ]       || { wget -q https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-devel-$V-mingw.tar.gz && tar xf SDL2-devel-$V-mingw.tar.gz; }
[ -d SDL2_ttf-$T ]   || { wget -q https://github.com/libsdl-org/SDL_ttf/releases/download/release-$T/SDL2_ttf-devel-$T-mingw.tar.gz && tar xf SDL2_ttf-$T.tar.gz 2>/dev/null || tar xf SDL2_ttf-devel-$T-mingw.tar.gz; }
[ -d SDL2_image-$I ] || { wget -q https://github.com/libsdl-org/SDL_image/releases/download/release-$I/SDL2_image-devel-$I-mingw.tar.gz && tar xf SDL2_image-devel-$I-mingw.tar.gz; }
echo "✅ SDL2 Windows 开发包就绪"
