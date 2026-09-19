#!/bin/bash
# autotrim.sh — 自动裁剪: 扫描源文件里用到的控件构造, 输出"未使用控件"的 -DFXTK_WIDGET_XXX=0 开关。
# 用法:  ./tools/autotrim.sh <源文件...>        (stdout 是加到编译命令里的裁剪开关)
#        CFLAGS="$(./tools/autotrim.sh ../examples/ex01_hello.c) -O2 -s"  ...
#
# 原理: 每个控件有对应的构造宏; 若源码里没出现该构造, 就把它的实现裁掉, 减小二进制。
#   支持从 .c 里识别: fx_button_new / fx_label_new / fx_grid_map / fx_canvas_new / fx_slider_new /
#   fx_progress_new / fx_checkbox_new / fx_panel_new / fx_tab_new / fx_image_new /
#   fx_textedit_new / fx_list_new / fx_drop_new   (含 *_p 变体)
set -eu

# ============================================================
# 自检:  ./autotrim.sh --selftest
# 回归保护 —— v2.4.5 之前的正则 "${sym}_p?\(" 把 '?' 挂在了 'p' 上, 要求字面量 "_p(",
# 于是【所有】控件都被判成"未使用", 生成 13 个裁剪开关, 框架随即编译失败
# (s_bt_t0 / s_pa_w undeclared)。当时症状极具迷惑性: 脚本退出码 0、开关看着"正常"。
# 这个自检不依赖网络/编译器, 只验证"两种写法都认得、真没用到的才裁"。
# 必须放在下面的参数校验【之前】, 否则 --selftest 会被当成"缺源文件"而直接退出。
# ============================================================
if [ "${1:-}" = "--selftest" ]; then
  T="$(mktemp)"; fails=0
  chk() { # $1=期望(keep/trim) $2=源码内容 $3=工厂宏(与 WIDGETS 里冒号后的名字一致)
    printf '%s\n' "$2" >"$T"
    if grep -qE "${3}_?p?\(" "$T"; then got=keep; else got=trim; fi
    if [ "$got" = "$1" ]; then printf '  [ok]   %-8s %s\n' "${3#fx_}" "$2"
    else printf '  [FAIL] %-8s %s (got=%s want=%s)\n' "${3#fx_}" "$2" "$got" "$1"; fails=$((fails+1)); fi
  }
  echo "🔍 autotrim 模式自检"
  # 注意参数顺序: chk <期望 keep|trim> <源码内容> <工厂宏>。
  # 曾把内容与符号写反, 结果 grep 拿"内容"当符号名 → 全部误判为 trim(踩过一次)。
  chk keep 'fx_button_new(pixel("1,1","2,2"), title("x"));' fx_button_new
  chk keep 'w = fx_list_new_p("1,1","2,2", 0);'             fx_list_new
  chk keep 'w = fx_drop_new("1,1","2,2");'                  fx_drop_new
  chk trim 'fx_button_new(pixel("1,1","2,2"));'             fx_label_new
  chk trim 'int a = 0; /* 没有画布 */'                       fx_canvas_new
  rm -f "$T"

  # 第二段: 逐个裁剪每个控件, 验证框架【真的能】在这些开关下编译。
  # 这一条抓的是另一类洞: 声明在 `#if FXTK_WIDGET_XXX` 内、却在守卫外被引用的静态数组
  # ——fxtk_anim_reset() 曾无条件 memset 四张动画槽表, 导致裁掉 TAB/BUTTON/PROGRESS
  # 任意一个都报 's_pa_w undeclared' 而编译失败(等于裁剪功能整体不可用)。
  # 需要 gcc; 缺失则跳过而非失败(受限环境).
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  if command -v gcc >/dev/null 2>&1 && [ -f "$ROOT/components/fxtk/fxtk_widgets.c" ]; then
    echo "🔍 裁剪开关编译自检 (13 个控件逐个 =0)"
    n=0
    for W in BUTTON LABEL GRID CANVAS SLIDER PROGRESS CHECKBOX PANEL TAB IMAGE TEXTEDIT LIST DROP; do
      if gcc -fsyntax-only -DFXTK_WIDGET_$W=0 -I"$ROOT/components/fxtk" \
             "$ROOT/components/fxtk/fxtk_widgets.c" >/dev/null 2>&1; then
        n=$((n+1))
      else
        echo "  [FAIL] FXTK_WIDGET_$W=0 编译失败 (守卫漏洞?)"; fails=$((fails+1))
      fi
    done
    [ "$fails" = 0 ] && echo "  [ok]   13/13 个控件都能单独裁剪"
  else
    echo "  [skip] 无 gcc, 跳过裁剪编译自检"
  fi

  if [ "$fails" = 0 ]; then echo "✅ autotrim 自检通过"; exit 0
  else echo "❌ autotrim 自检失败 ($fails 项)"; exit 1; fi
fi

[ $# -ge 1 ] || { echo "用法: $0 <源文件...>   (自检: $0 --selftest)" >&2; exit 1; }
SRCS="$@"

WIDGETS="button:fx_button_new label:fx_label_new grid:fx_grid_map canvas:fx_canvas_new \
slider:fx_slider_new progress:fx_progress_new checkbox:fx_checkbox_new panel:fx_panel_new \
tab:fx_tab_new image:fx_image_new textedit:fx_textedit_new list:fx_list_new drop:fx_drop_new"

out=""
for w in $WIDGETS; do
    type="${w%%:*}"; sym="${w##*:}"
    # 该工厂宏是否出现在任何源文件里? (list/drop 用 fx_list_new / fx_drop_new, 会同时匹配 _p 变体)
    # v2.3.1 引入、v2.4.5 修正: 要匹配 fx_list_new( 与 fx_list_new_p( 两种写法。
    #   ❌ 旧写法 "${sym}_p?\(" 是错的 —— ERE 里 ? 只作用于紧邻的单个字符 'p',
    #      于是它要求字面量 "…_p("，反而【不】匹配 fx_button_new(
    #      ⇒ 所有控件都被判为"未使用" ⇒ 13 个裁剪开关全开 ⇒ 框架编译失败
    #      (症状: fxtk_widgets.c 里 s_bt_t0 / s_pa_w 等未声明)。
    #   ✅ 正确写法 "${sym}_?p?\(" —— 下划线与 p 都可选, 两种写法都匹配。
    # 这是"静默失败"的典型: 开关生成成功、退出码 0, 只有编译时才炸。
    if ! grep -qE "${sym}_?p?\(" $SRCS; then
        out="$out -DFXTK_WIDGET_$(echo "$type" | tr 'a-z' 'A-Z')=0"
    fi
done
