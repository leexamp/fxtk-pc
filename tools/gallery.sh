#!/bin/bash
# gallery.sh — 生成 fxtk 截图画廊 (v2.4)
#
#   ./tools/gallery.sh [输出目录]        默认 /tmp/fxtk-gallery
#
# 产出:
#   demo_p0.png … demo_p11.png            演示 12 个页面 (1280x720, 无头 SDL dummy)
#   canvas_XX_<W>x<H>.png                 7 个画布教程示例 × 2 个分辨率 (纯软件假驱动, 可进 CI 金图)
#
# 说明:
#   - 全部无头 (SDL_VIDEODRIVER=dummy / 假驱动), 不需要显示器, 可直接在 CI 跑。
#   - 组件页 (demo_p11) 读真实文件系统 + 悬停状态, 【天然非确定】, 仅供人工评审, 不可做像素基线。
#   - 走 fx_screenshot() (驱动的 read_pixels 钩子) → backends 的 stb PNG 编码, 零外部依赖。
set -eu
OUT=${1:-/tmp/fxtk-gallery}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.ppm

echo "🔨 构建工具..."
( cd demo-main && make -s test/bench >/dev/null 2>&1 || make test/bench >/dev/null )

echo "📸 演示页面 (12 页 @1280x720)..."
( cd demo-main
  for p in $(seq 0 11); do
    SDL_VIDEODRIVER=dummy ./test/bench 30 "$p" 1000 1280 720 "$OUT/demo_p$p.png" >/dev/null 2>&1 || true
  done )

echo "📸 画布示例 (7 例 × 2 分辨率, 软件渲染)..."
CORE="../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c \
      ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c"
for src in examples/canvas/canvas_*.c; do
    name=$(basename "$src" .c)
    ( cd demo-main
      gcc -O2 -I. -I../components/fxtk $CORE test/render_canvas.c "../$src" -o /tmp/fxtk_gallery_rc -lm 2>/dev/null || { echo "  ⚠ 编译失败: $src"; continue; }
      for res in "480 272" "1280 720"; do
        set -- $res
        /tmp/fxtk_gallery_rc "$1" "$2" "$OUT/${name}_${1}x${2}.png" >/dev/null 2>&1 || true
      done )
done

n=$(ls -1 "$OUT"/*.png 2>/dev/null | wc -l)
echo "✅ 画廊已生成: $n 张 PNG → $OUT"
ls -1 "$OUT"/*.png 2>/dev/null | head -20
