/** ex14_resize — 窗口拖拽布局跟随, 无残影 */
#include "fxtk.h"
#include <stdio.h>
#include <math.h>
static void on_info(fx_widget_t*w,void*ud){
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    fx_set_color(FX_RGB(250,250,250)); fx_fill_rect(0,0,cw-1,ch-1);
    char b[64]; snprintf(b,sizeof b,"逻辑尺寸: %d x %d  (拉窗口试试)",cw,ch);
    fx_draw_text_c(10,10,b,FX_RGB(40,40,40),FX_RGB(250,250,250));
}
static void on_wave(fx_widget_t*w,void*ud){
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    fx_set_color(FX_BLACK); fx_fill_rect(0,0,cw-1,ch-1);
    fx_set_color(FX_YELLOW);
    static int t=0;
    for(int x=0;x<cw;x+=2){
        int y=ch/2+(int)(sinf((x+t)*0.08f)*ch/3);
        fx_draw_vline(x,y,y);
    }
    t++;
}
void app_init(void){
    fx_set_bg(FX_RGB(240,240,240));
    fx_canvas_new(percent("0.02,0.02","0.98,0.20"), anim(1), call(on_info));
    fx_canvas_new(percent("0.02,0.24","0.98,0.98"), color(FX_BLACK), anim(1), call(on_wave));
}
