/** ex10_extra — 列表/下拉/多字号 演示 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>

static fx_widget_t *s_pick;
static const char *s_names[] = { "张伟","王芳","李娜","刘洋","陈静","杨帆",
                                 "赵磊","黄敏","周涛","吴婷","Alice","Bob" };

static void on_pick(fx_widget_t *w, void *ud)
{
    char b[48];
    snprintf(b, sizeof(b), "选中: %s (#%d)", s_names[(int)(intptr_t)ud], (int)(intptr_t)ud);
    fx_set_title(s_pick, b);
}
static void on_drop(fx_widget_t *w, void *ud)
{
    static const int sz[3] = { 14, 18, 24 };
    int i = (int)(intptr_t)ud; if (i < 0 || i > 2) i = 1;
    fx_widget_t *big = fx_find("big");
    if (big) fx_set_fontsize(big, sz[i]);
}

void app_init(void)
{
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_set_window_title("ex10 列表/下拉/字号");
    fx_label_new(pixel("10,6", "470,30"), line(20), row(1),
                 title("line(20) 大字号 + row(1) 居中"), fgcolor(FX_RGB(40, 40, 40)));
    fx_label_new(pixel("10,32", "240,50"), line(13),
                 title("line(13) 小字号左对齐"), fgcolor(FX_RGB(110, 110, 110)));
    fx_label_new(pixel("250,32", "470,50"), line(16), row(2),
                 title("row(2) 右对齐"), fgcolor(FX_RGB(110, 110, 110)));
    fx_label_new(pixel("10,54", "470,86"), name("big"), line(18), row(1),
                 title("下拉切换我的字号"), fgcolor(FX_RGB(33, 100, 200)));

    fx_widget_t *list = fx_list_new("10,92", "200,222");
    for (int i = 0; i < 12; i++) fx_list_add(list, s_names[i]);
    fx_list_set_cb(list, on_pick);

    fx_widget_t *drop = fx_drop_new("220,92", "360,118");
    fx_drop_add(drop, "字体: 小"); fx_drop_add(drop, "字体: 中"); fx_drop_add(drop, "字体: 大");
    fx_list_set_cb(drop, on_drop);

    s_pick = fx_label_new(pixel("220,130", "470,156"), name("picked"), line(16),
                          title("选中: -"), fgcolor(FX_RGB(33, 100, 200)));
    fx_label_new(pixel("10,230", "470,262"), line(12),
                 title("列表滚轮滚动+点击选中; 下拉靠底部自动向上弹, 弹层带滚动不越界。"),
                 fgcolor(FX_RGB(120, 120, 120)));
}
