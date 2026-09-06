/** ex11_percent — percent(0.0~1.0) 铺满/对齐, 与 pixel 对照 */
#include "fxtk.h"
static void on_fill(fx_widget_t*w,void*ud){
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    fx_set_color(FX_RGB(33,150,243)); fx_fill_rect(0,0,cw-1,ch-1);
    fx_set_color(FX_WHITE);
    for(int x=0;x<cw;x+=24) fx_draw_line(x,0,x+12,ch-1);
}
static void on_btn(fx_widget_t*w,void*ud){ fx_set_title(w,"被点"); }
void app_init(void){
    fx_set_bg(FX_RGB(240,240,240));
    fx_label_new(percent("0.02,0.02","0.98,0.10"), row(1), line(16),
                 title("percent 铺满标题"), fgcolor(FX_RGB(51,51,51)));
    fx_canvas_new(percent("0.02,0.12","0.98,0.60"), color(FX_RGB(33,150,243)), call(on_fill));
    fx_button_new(percent("0.02,0.64","0.30,0.74"), title("左"), call(on_btn));
    fx_button_new(percent("0.36,0.64","0.64,0.74"), title("中"), call(on_btn));
    fx_button_new(percent("0.70,0.64","0.98,0.74"), title("右"), call(on_btn));
    fx_label_new(pixel("10,250","470,266"), line(10),
                 title("上为 percent 铺满, 本行为 pixel 固定"), fgcolor(FX_RGB(120,120,120)));
}
