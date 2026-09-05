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
[ $# -ge 1 ] || { echo "用法: $0 <源文件...>" >&2; exit 1; }
SRCS="$@"

WIDGETS="button:fx_button_new label:fx_label_new grid:fx_grid_map canvas:fx_canvas_new \
slider:fx_slider_new progress:fx_progress_new checkbox:fx_checkbox_new panel:fx_panel_new \
tab:fx_tab_new image:fx_image_new textedit:fx_textedit_new list:fx_list_new drop:fx_drop_new"

out=""
for w in $WIDGETS; do
    type="${w%%:*}"; sym="${w##*:}"
    # 该工厂宏是否出现在任何源文件里? (list/drop 用 fx_list_new / fx_drop_new, 会同时匹配 _p 变体)
    if ! grep -qE "${sym}\(" $SRCS; then
        out="$out -DFXTK_WIDGET_$(echo "$type" | tr 'a-z' 'A-Z')=0"
    fi
done
echo "$out"
