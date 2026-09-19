#!/bin/bash
# ============================================================
# golden.sh — v2.4 金图回归 (无头, 可进 CI)
#
#   ./tools/golden.sh                回归: 渲染并与 test/golden/ 参考图逐像素比对
#   ./tools/golden.sh --update       重新生成参考图(仅在"刻意改动"后使用, 并在提交信息里说明)
#   ./tools/golden.sh --tol 4        容忍每个通道 4 级差(跨驱动/跨编码时用)
#   GOLDEN_SKIP_DEMO=1 ./tools/golden.sh    只跑画布示例(最快)
#
# 为什么能进 CI: 画布示例走纯软件假驱动, 不依赖显示器/GPU, 输出确定;
# 演示页走 SDL dummy 驱动 + 固定帧数, 同样是确定性的(第 11 页读真实文件系统, 已排除)。
# ============================================================
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
GOLD="$ROOT/test/golden"
TMP="${TMPDIR:-/tmp}/fxtk-golden"
TOL=0; UPDATE=0
for a in "$@"; do
  case "$a" in
    --update) UPDATE=1 ;;
    --tol) shift; TOL=${1:-0} ;;
    --tol=*) TOL=${a#--tol=} ;;
  esac
done
# 上面 for 里 shift 的写法在 set -u 下不安全, 单独解析 --tol
TOL=${GOLDEN_TOL:-$TOL}

mkdir -p "$TMP" "$GOLD"
[ -x /tmp/imgdiff ] || gcc -O2 -I. tools/imgdiff.c -o /tmp/imgdiff -lm
CORE="../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c ../components/fxtk/fxtk_widgets.c
      ../components/fxtk/fxtk_effects.c ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c"
fail=0; n=0

echo "📐 画布示例 (480x272, 纯软件驱动)"
for src in examples/canvas/canvas_*.c; do
  name=$(basename "$src" .c)
  ( cd demo-main
    gcc -O2 -I. -I../components/fxtk $CORE test/render_canvas.c "../$src" -o /tmp/fxtk_golden_rc -lm 2>/dev/null ) || { echo "  ⚠ 编译失败 $name"; fail=1; continue; }
  /tmp/fxtk_golden_rc 480 272 "$TMP/$name.png" >/dev/null 2>&1 || { echo "  ⚠ 渲染失败 $name"; fail=1; continue; }
  n=$((n+1))
  if [ "$UPDATE" = "1" ]; then cp "$TMP/$name.png" "$GOLD/$name.png"; echo "  ✏️  更新 $name.png"; continue; fi
  if [ ! -f "$GOLD/$name.png" ]; then echo "  ⚠ 缺参考图 $name.png (先跑 --update)"; fail=1; continue; fi
  # 注意: 不能写成 `imgdiff ... | sed ... || fail=1` —— 管道的退出码是 sed 的(恒为 0),
  # 会把 imgdiff 的失败吞掉, 于是"有差异"时仍打印 ✅ 通过并以 0 退出(v2.4.5 修复)。
  # 这里先把输出写文件、单独取退出码, 再统一加前缀, 保证 fail 一定被置位。
  /tmp/imgdiff "$TMP/$name.png" "$GOLD/$name.png" "$TMP/${name}_diff.png" "$TOL" >"$TMP/$name.diff.txt" 2>&1 || fail=1
  sed "s/^/  $name:/" "$TMP/$name.diff.txt"
done

if [ "${GOLDEN_SKIP_DEMO:-0}" != "1" ]; then
  echo "🖥  演示页 (1280x720, SDL dummy; 非确定的 p4/p11 已排除)"
  ( cd demo-main && make -s test/bench >/dev/null 2>&1 || make test/bench >/dev/null )
  # 排除 p4(3D 页会画 FPS/耗时标签, 帧边界上会变)与 p11(读真实文件系统+悬停): 天然非确定
  for p in 0 1 2 3 5 6 7 8 9 10; do
    ( cd demo-main && SDL_VIDEODRIVER=dummy ./test/bench 30 "$p" 1000 1280 720 "$TMP/demo_p$p.png" >/dev/null 2>&1 ) || { echo "  ⚠ 渲染失败 demo_p$p"; fail=1; continue; }
    n=$((n+1))
    if [ "$UPDATE" = "1" ]; then cp "$TMP/demo_p$p.png" "$GOLD/demo_p$p.png"; echo "  ✏️  更新 demo_p$p.png"; continue; fi
    if [ ! -f "$GOLD/demo_p$p.png" ]; then echo "  ⚠ 缺参考图 demo_p$p.png"; fail=1; continue; fi
    # 同画布循环: 见上方注释 —— 管道会吞掉 imgdiff 的退出码。
    /tmp/imgdiff "$TMP/demo_p$p.png" "$GOLD/demo_p$p.png" "$TMP/demo_p${p}_diff.png" "$TOL" >"$TMP/demo_p$p.diff.txt" 2>&1 || fail=1
    sed "s/^/  demo_p$p:/" "$TMP/demo_p$p.diff.txt"
  done
fi

if [ "$UPDATE" = "1" ]; then echo "✅ 参考图已更新: $n 张 → test/golden/"; exit 0; fi
if [ "$fail" = "0" ]; then echo "✅ 金图回归通过: $n 张全部一致 (容差 $TOL)"; else echo "❌ 金图回归失败: 有图不一致(差异图在 $TMP/*_diff.png)"; fi
exit $fail
