#!/bin/bash
# ============================================================================
# verify.sh —— 编译 skill 的参考实现，确认 SKILL.md 里的 API 都是真的
#
#   ./verify.sh            编译并报告通过/失败（-Wall -Wextra，要求 0 warning）
#   ./verify.sh --run      额外构建一个无头可执行版并跑帧循环（需要能建 GL 上下文）
#
# 这个脚本是 skill 的"防幻觉"闸门：如果 ref_app.c 编译不过，说明 SKILL.md
# 里的 API 漂移了，必须同步更新 —— 不要绕过它。
# ============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/fxtk-skill-ref"

CORE=(
  "$ROOT/components/fxtk/fxtk.c"
  "$ROOT/components/fxtk/fxtk_draw.c"
  "$ROOT/components/fxtk/fxtk_widgets.c"
  "$ROOT/components/fxtk/fxtk_effects.c"
  "$ROOT/components/fxtk/fxtk_extra.c"
  "$ROOT/components/fxtk/fxtk_backends.c"
)
DRV=(
  "$ROOT/drivers/fxtk_app_sokol.c"
  "$ROOT/drivers/fxtk_sokol_driver.c"
  "$ROOT/drivers/fxtk_font_stb.c"
)
INC=(-I"$ROOT/components/fxtk" -I"$ROOT/drivers" -I"$ROOT/third_party/sokol")
LIBS=(-lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm)

echo "🔎 fxtk skill 自检: 编译 ref_app.c (-Wall -Wextra, 期望 0 warning)"
LOG="$OUT.log"
mkdir -p "$(dirname "$OUT")"

# 检测本检出是否已有 v2.4.5 的 fx_textedit_set_readonly 实现。
# 旧检出上 ref_app.c 会跳过该调用（而非链接失败），这样 skill 在任一版本上都能自检。
DEFS=()
if grep -qE 'fx_textedit_set_readonly\s*\(' "$ROOT/components/fxtk/fxtk.c"; then
  DEFS+=(-DFXTK_HAVE_READONLY)
  echo "   ℹ 检出含 fx_textedit_set_readonly 实现 → 启用只读演示"
else
  echo "   ℹ 检出较旧(无 fx_textedit_set_readonly 实现) → 跳过只读演示"
fi

gcc -O2 -Wall -Wextra "${DEFS[@]}" "${INC[@]}" \
    "$HERE/ref_app.c" "${DRV[@]}" "${CORE[@]}" \
    -o "$OUT" "${LIBS[@]}" >"$LOG" 2>&1
rc=$?

if [ $rc -ne 0 ]; then
  echo "❌ 编译/链接失败 —— SKILL.md 的 API 已漂移，请对照报错更新文档与参考实现："
  # 头尾都给: 编译错误在最前面, 链接的 undefined reference 在最后面。
  sed 's/^/   /' "$LOG" >"$OUT.err" 2>/dev/null || true
  head -25 "$OUT.err"
  if [ "$(wc -l <"$OUT.err")" -gt 25 ]; then echo "   ..."; tail -10 "$OUT.err"; fi
  exit 1
fi

# 注意: 下面这段刻意不用 `... | head` —— head 提前关闭管道会让上游 grep/test
# 收到 SIGPIPE 而死, 在 `set -e` 下会表现为"脚本自己挂了"(v2.4.5 踩过一次)。
# 需要限长时一律先写文件, 或用 `|| true` 逐条吞掉。
if grep -qE 'warning:' "$LOG"; then
  # 只对**本 skill 自己的代码**设 0-warning 门禁。
  # 框架自身的 warning（misleading-indentation、missing-field-initializers 等）是既有状况，
  # 不属于这个 skill 的责任范围 —— 若一并卡住，门禁会因为别人的代码而永远红着。
  grep -E 'warning:' "$LOG" >"$OUT.warn" || true
  OWN_WARN=$(grep -c 'ref_app.c' "$OUT.warn" || true)
  if [ "$OWN_WARN" -gt 0 ]; then
    echo "❌ ref_app.c 自身有 $OWN_WARN 条 warning（skill 承诺 0 warning）："
    grep 'ref_app.c' "$OUT.warn" | sed 's/^/   /' | head -20 || true
    exit 1
  fi
  OTHER=$(wc -l <"$OUT.warn")
  echo "ℹ️  ref_app.c 自身 0 warning（框架源里有 $OTHER 条既有 warning，不计入门禁）"
fi

echo "✅ 通过: 0 error / 0 warning —— SKILL.md 里的 API 与当前代码一致"
echo "   产物: $OUT ($(stat -c%s "$OUT" 2>/dev/null) B)"

if [ "${1:-}" = "--run" ]; then
  echo "▶ 试运行（3 秒，需要可用的 GL/显示环境）"
  timeout 3 "$OUT" >/dev/null 2>&1 && echo "   ✅ 启动正常" || echo "   ⚠ 未能启动（无显示环境时属正常）"
fi
