#!/bin/bash
# ============================================================================
# verify.sh —— 编译 skill 自带的所有程序，确认 REFERENCE.md 里的 API 都是真的
#
#   ./verify.sh            编译 + 跑逻辑单测，报告通过/失败（要求 0 warning）
#   ./verify.sh --run      额外试运行 GUI 程序 3 秒（需要可用的 GL/显示环境）
#
# 这个脚本是 skill 的"防幻觉"闸门：自带程序编译不过，就说明 REFERENCE.md
# 里的 API 漂移了，必须同步更新 —— 不要绕过它。
#
# 实现要点（都是踩出来的，别改回去）：
#   1. 框架源只编译一次成 .o，两个程序复用 —— 否则 3 个 gcc 调用把自检拖到 1 分钟以上。
#   2. 自带程序单独 -c 编译，warning 收集干净，不会把框架的既有 warning 算到它头上。
#   3. 不对框架源设 0-warning 门禁：那 47 条既有 warning 不归这个 skill 管，
#      一并卡住会让门禁因为别人的代码永远红着，反而失去意义。
#   4. 不用 `... | head`：head 提前关管道会让上游 grep 收 SIGPIPE 而死，
#      在 set -e 下表现为"脚本自己挂了"。需要限长一律先写文件。
# ============================================================================
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/fxtk-skill-verify"
OBJ="$OUT.obj"          # 框架目标文件（只放框架）
MAIN="$OUT.main"        # 自带程序的目标文件（与框架分开，避免互相污染）
rm -rf "$OBJ" "$MAIN"
mkdir -p "$OBJ" "$MAIN" "$(dirname "$OUT")"

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
CFLAGS=(-O2 -Wall -Wextra)

die_log() {   # $1=日志文件 $2=标题
  echo "❌ $2"
  sed 's/^/   /' "$1" >"$1.pretty" 2>/dev/null || true
  head -25 "$1.pretty"
  if [ "$(wc -l <"$1.pretty")" -gt 25 ]; then echo "   ..."; tail -10 "$1.pretty"; fi
  exit 1
}

echo "🔎 fxtk skill 自检 (-Wall -Wextra, 自带程序要求 0 warning)"

# ---- 0) 探测本检出版本能力 ----------------------------------------------
DEFS=()
if grep -qE 'fx_textedit_set_readonly\s*\(' "$ROOT/components/fxtk/fxtk.c"; then
  DEFS+=(-DFXTK_HAVE_READONLY)
  echo "   ℹ 检出含 fx_textedit_set_readonly 实现 → 启用只读演示"
else
  echo "   ℹ 检出较旧(无 fx_textedit_set_readonly 实现) → 跳过只读演示"
fi

# ---- 1) 框架源编译一次（warning 不计入门禁，只报告数量） -------------------
FWLOG="$OUT.framework.log"
: >"$FWLOG"
for s in "${CORE[@]}" "${DRV[@]}"; do
  o="$OBJ/$(basename "${s%.c}").o"
  gcc "${CFLAGS[@]}" "${DEFS[@]}" "${INC[@]}" -c "$s" -o "$o" >>"$FWLOG" 2>&1 \
    || die_log "$FWLOG" "框架源编译失败（这是仓库本身的问题，不是 skill 的）：$s"
done
FWWARN=$(grep -c 'warning:' "$FWLOG" || true)
echo "   ℹ 框架源编译完成（$FWWARN 条既有 warning，不计入门禁）"

# ---- 2) 自带 GUI 程序逐个过闸 --------------------------------------------
for SRC in ref_app.c calc.c; do
  NAME="${SRC%.c}"
  SLOG="$OUT.$NAME.log"
  echo "   ▸ $SRC"

  gcc "${CFLAGS[@]}" "${DEFS[@]}" "${INC[@]}" -c "$HERE/$SRC" -o "$MAIN/$NAME.o" >"$SLOG" 2>&1 \
    || die_log "$SLOG" "$SRC 编译失败 —— REFERENCE.md 的 API 已漂移，请对照报错更新文档与代码"

  # 本文件自身的 warning：干净归属，不与框架混淆
  if grep -qE 'warning:' "$SLOG"; then
    echo "❌ $SRC 自身有 warning（skill 承诺 0 warning）："
    grep -E 'warning:' "$SLOG" | sed 's/^/   /' | head -20 || true
    exit 1
  fi

  # 只链【框架对象】+ 本程序自己的对象。
  # 注意别用 $OBJ/*.o 一把梭：两个程序都定义 fxtk_app_init，
  # 把彼此的主对象也链进去会报 multiple definition（踩过一次）。
  gcc "$MAIN/$NAME.o" "$OBJ"/*.o -o "$OUT.$NAME" "${LIBS[@]}" >>"$SLOG" 2>&1 \
    || die_log "$SLOG" "$SRC 链接失败（多为 undefined reference：用了不存在或版本不符的 API）"
  echo "     0 warning（产物 $(stat -c%s "$OUT.$NAME" 2>/dev/null) B）"
done

# ---- 3) 计算器求值逻辑的纯逻辑单测（无 GUI） ------------------------------
echo "   ▸ calc_logic_test.c (逻辑单测)"
LLOG="$OUT.logic.log"
gcc "${CFLAGS[@]}" "$HERE/calc_logic_test.c" -o "$OUT.logic" -lm >"$LLOG" 2>&1 \
  || die_log "$LLOG" "calc_logic_test.c 编译失败"
"$OUT.logic" >"$OUT.logic.out" 2>&1 || {
  echo "❌ 计算器逻辑单测未通过："
  sed 's/^/   /' "$OUT.logic.out" | tail -15
  exit 1
}
grep -E '^(PASS|FAIL)' "$OUT.logic.out" | sed 's/^/     /'

echo "✅ 通过: 0 error / 0 warning —— REFERENCE.md 的 API 与当前代码一致"

# ---- 4) 可选：试运行 ------------------------------------------------------
if [ "${1:-}" = "--run" ]; then
  echo "▶ 试运行 GUI（3 秒，需要可用的 GL/显示环境）"
  timeout 3 "$OUT.ref_app" >/dev/null 2>&1 && echo "   ✅ 启动正常" \
    || echo "   ⚠ 未能启动（无显示环境时属正常）"
fi
