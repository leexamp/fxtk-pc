#!/bin/bash
# ============================================================
# build.sh — 一键构建并运行演示 (中英文文档都指向这个入口)
#
#   ./build.sh              构建并运行 (默认后端 = sokol)
#   ./build.sh --no-run     只构建
#   ./build.sh --sdl        构建并运行遗留 SDL 版 (对照用)
#   ./build.sh --shared     打动态库包 (exe 只留 app 层, 见 build_shared.sh)
#   ./build.sh --win        交叉编译 Windows 单 exe (见 build_win_sokol.sh)
#
# 为什么改成薄封装: 旧版本是 v2.2 手写的 gcc 长命令(硬编码 SDL2 依赖、绕过 Makefile),
# 结果文档说"sokol 是默认后端"、这个入口却给的是遗留 SDL 版 —— 两套构建定义必然漂移。
# 现在构建规则只留 Makefile 一处, 这里只负责挑目标 + 跑起来。
# ============================================================
set -e
cd "$(dirname "$0")"
MODE="${1:-}"
case "$MODE" in
  --no-run) make && exit 0 ;;
  --sdl)    make fxtk_sim_sdl && exec ./fxtk_sim_sdl ;;
  --shared) exec ./build_shared.sh ;;
  --win)    exec ./build_win_sokol.sh ;;
esac
make
exec ./fxtk_sim
