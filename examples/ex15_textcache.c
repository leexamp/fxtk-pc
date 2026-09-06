/** ex15_textcache — 多字号混排 + 字号滑杆, 验证字体缓存淘汰不闪断 */
#include "fxtk.h"
#include <stdio.h>
static fx_widget_t *s_big;
static void on_sz(fx_widget_t*w,void*ud){ fx_set_fontsize(s_big, 10+fx_get_value(w)/4); }
static void on_cv(fx_widget_t*w,void*ud){
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    fx_set_color(FX_WHITE); fx_fill_rect(0,0,cw-1,ch-1);
    int y=8;
    for(int s=10;s<=28;s+=3){
        char b[40]; snprintf(b,sizeof b,"字号 %d 中文混排 ABC",s);
        fxtk_draw_text_size(s,8,y,b,FX_BLACK,FX_WHITE);
        y+=s+6;
    }
}
void app_init(void){
    fx_set_bg(FX_RGB(240,240,240));
    s_big=fx_label_new(pixel("10,10","470,40"), line(14), title("拖滑杆改我字号"),fgcolor(FX_RGB(60,60,60)));
    fx_slider_new(pixel("10,50","200,64"), value(40), call(on_sz));
    fx_canvas_new(pixel("10,80","470,262"), anim(1), call(on_cv));
}
