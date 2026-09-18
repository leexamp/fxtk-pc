#!/bin/bash
# build.sh — your_app 一键构建/运行
#   ./build.sh            构建并运行
#   ./build.sh --no-run   只构建
#   ./build.sh --release  构建 + 打一个可分发目录(二进制与自己需要的资源)
set -e
cd "$(dirname "$0")"
case "${1:-}" in
  --no-run) make && exit 0 ;;
  --release)
    make
    rm -rf dist && mkdir -p dist
    cp your_app dist/
    cp ../README.md dist/ 2>/dev/null || true
    printf 'your_app —— fxtk 起步示例\n\n运行: ./your_app\n依赖: 系统 OpenGL / X11(无第三方动态库)\n' > dist/运行说明.txt
    echo "✅ 已打包到 your_app/dist/"; ls -la dist | tail -n +2 ;;
  *) make && exec ./your_app ;;
esac
