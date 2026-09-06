/** ex01_hello — 按钮/标签/复选框入门 (主题联动: 背景+前景一起换) */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>

static int s_n = 0;
static int s_dark = 0;

static void apply_theme(void)
{
    fx_color_t bg = s_dark ? FX_RGB(30, 30, 34) : FX_RGB(240, 240, 240);
    fx_color_t fg = s_dark ? FX_RGB(235, 235, 235) : FX_RGB(40, 40, 40);
    fx_widget_t *ws[3] = { fx_find("ttl"), fx_find("lab"), fx_find("chk") };
    for (int i = 0; i < 3; i++) {
        fx_set_color_w(ws[i], bg);   /* 先改控件 */
        fx_set_fgcolor(ws[i], fg);
    }
    fx_set_bg(bg);   /* 最后改窗口: 全屏重绘压轴, 不被脏区降级 */
}
static void on_btn(fx_widget_t *w, void *ud) {
    s_n++;
    char b[32]; snprintf(b, sizeof(b), "已点击 %d 次", s_n);
    fx_set_title(fx_find("lab"), b);
}
static void on_chk(fx_widget_t *w, void *ud) {
    s_dark = fx_get_value(w);
    apply_theme();
}
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_label_new(percent("0.05,0.06", "0.95,0.18"), name("ttl"),
                 title("fxtk 示例 01 — Hello, 世界!"),
                 color(FX_RGB(240, 240, 240)), fgcolor(FX_RGB(40, 40, 40)));
    fx_button_new(pixel("60,80", "200,120"), title("点我"), color(FX_RGB(33, 150, 243)), call(on_btn));
    fx_label_new(pixel("60,140", "300,165"), name("lab"), title("已点击 0 次"),
                 color(FX_RGB(240, 240, 240)), fgcolor(FX_RGB(40, 40, 40)));
    fx_checkbox_new(pixel("60,180", "220,205"), name("chk"), title("深色背景"),
                    color(FX_RGB(240, 240, 240)), fgcolor(FX_RGB(40, 40, 40)), call(on_chk));
}
