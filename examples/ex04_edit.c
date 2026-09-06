/** ex04_edit — 输入框: 框选/剪贴板/字数限制/滚轮 */
#include "fxtk.h"
#include "fxtk_desktop.h"
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_label_new(pixel("10,15", "470,35"), title("拖拽框选 / Ctrl+A C V X / Backspace / 滚轮滚动"), fgcolor(FX_RGB(60, 60, 60)));
    fx_textedit_new(pixel("10,45", "470,110"), name("e1"), title("hello 你好, 拖拽选中我, 滚轮滚动我"));
    fx_textedit_new(pixel("10,120", "470,155"), name("e2"), title(""), maxlen(20));
    fx_label_new(pixel("10,165", "470,190"), title("上一框限 20 字并带计数"), fgcolor(FX_RGB(120, 120, 120)));
}
