/** ex12_hover — fx_touch_state 悬停实时坐标 + 悬停高亮 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
static void on_cv(fx_widget_t*w,void*ud){
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1,ch=y2-y1+1;
    fx_set_color(FX_RGB(30,30,30)); fx_fill_rect(0,0,cw-1,ch-1);
    int mx,my,mp; fx_touch_state(&mx,&my,&mp);
    int lx=mx-x1, ly=my-y1;
    for(int i=0;i<3;i++){
        int bw=(cw-40)/3-10, bx=20+i*((cw-40)/3), by=20, bh=ch/3;
        int hov = lx>=bx&&lx<=bx+bw&&ly>=by&&ly<=by+bh;
        fx_set_color(hov?FX_RGB(33,150,243):FX_RGB(70,70,70));
        fx_fill_rect(bx,by,bx+bw,by+bh);
    }
    char b[64]; snprintf(b,sizeof b,"鼠标: %d,%d  %s",lx,ly,mp?"按下":"悬停");
    fx_draw_text_c(10,ch-24,b,FX_GREEN,FX_RGB(30,30,30));
}
void app_init(void){
    fx_set_bg(FX_RGB(240,240,240));
    fx_canvas_new(pixel("10,10","470,262"), anim(1), call(on_cv));
}
