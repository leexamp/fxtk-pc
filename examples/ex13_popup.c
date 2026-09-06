/** ex13_popup — 底部下拉自动向上弹 + 输入框右键置顶菜单 */
#include "fxtk.h"
#include "fxtk_desktop.h"
static void on_drop(fx_widget_t*w,void*ud){}
void app_init(void){
    fx_set_bg(FX_RGB(240,240,240));
    fx_label_new(pixel("10,10","470,30"), line(14),
                 title("右键输入框=置顶菜单; 底部下拉自动向上弹"), fgcolor(FX_RGB(60,60,60)));
    fx_textedit_new(pixel("10,40","470,70"), title("右键我"));
    fx_widget_t *d=fx_drop_new("10,230","200,256");
    fx_drop_add(d,"选项 A"); fx_drop_add(d,"选项 B"); fx_drop_add(d,"选项 C");
    fx_list_set_cb(d,on_drop);
}
