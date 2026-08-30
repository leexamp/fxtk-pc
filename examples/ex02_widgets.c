/** ex02_widgets — 网格键盘/滑条/进度条联动 */
#include "fxtk.h"
static void on_key(fx_widget_t *w, void *ud) {
    int v = (fx_get_value(fx_find("sl")) + 10) % 110;
    fx_set_value(fx_find("sl"), v);
    fx_set_value(fx_find("pg"), v);
}
static void on_sl(fx_widget_t *w, void *ud) { fx_set_value(fx_find("pg"), fx_get_value(w)); }
void app_init(void) {
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_grid_map(pixel("20,20", "260,200"), line(3), row(3), name("pad"));
    /* grid 默认自带浅底(245,245,245) + 浅灰网格线, 无需手动铺背景;
       dense() 可选, 让 grid_map 紧凑排布 */
    const char *k[9] = { "1","2","3","4","5","6","7","8","9" };
    for (int i = 0; i < 9; i++)
        fx_button_new(grid("pad", i/3+1, i%3+1, i/3+1, i%3+1),
                      title(k[i]), color(FX_RGB(33, 150, 243)), call(on_key));
    fx_slider_new(pixel("280,40", "440,60"), name("sl"), color(FX_RGB(76, 175, 80)), call(on_sl));
    fx_progress_new(pixel("280,80", "440,96"), name("pg"));
    fx_label_new(pixel("280,120", "440,150"), title("拖滑条 / 按键盘联动"), fgcolor(FX_RGB(60, 60, 60)));
}
