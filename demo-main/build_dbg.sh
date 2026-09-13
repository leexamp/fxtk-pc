#!/bin/bash
# build_dbg.sh — 构建"取证版"演示: 越界图元探针直接编进二进制, 不依赖环境变量。
#   ./build_dbg.sh            构建并运行
#   ./build_dbg.sh --no-run   只构建
# 用法: 复现问题后, 终端里的 [rectdbg] 行(同时走 stdout 与 stderr)就是证据。
set -e
cd "$(dirname "$0")"
make fxtk_sim_dbg
[ "$1" = "--no-run" ] || exec ./fxtk_sim_dbg
