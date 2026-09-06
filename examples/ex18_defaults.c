/** ex18_defaults — 默认配色零配置 (不写 color()/fgcolor() 也好看) */
#include "fxtk.h"
#include <stdio.h>

static void on_btn(fx_widget_t *w, void *ud) {
    (void)ud;
    static int n = 0;
    char b[32]; snprintf(b, sizeof(b), "点击 %d 次", ++n);
    fx_set_title(w, b);
}
static void on_sl(fx_widget_t *w, void *ud) {
    (void)ud;
    fx_set_value(fx_find("prog"), fx_get_value(w));
}
void app_init(void) {
    /* 不调用 fx_set_bg, 窗口默认浅灰 240,240,240 */
    fx_label_new(percent("0.05,0.06", "0.95,0.16"),
                 title("默认配色示例 — 全部零 color()/fgcolor()"));
    fx_button_new(pixel("30,60", "210,100"), title("默认蓝按钮"), call(on_btn));
    fx_label_new(pixel("30,110", "400,140"), title("默认深字标签"));
    fx_slider_new(pixel("30,150", "300,170"), value(60), call(on_sl));
    fx_progress_new(pixel("30,180", "300,200"), name("prog"), value(60));
    fx_checkbox_new(pixel("30,210", "220,234"), title("默认复选框"));
    fx_label_new(pixel("30,240", "450,262"), line(10),
                 title("grid / panel / canvas 也都是浅底, 无需手动调背景"));
    fx_grid_map(pixel("320,110", "460,260"), line(3), row(3), dense());
}
