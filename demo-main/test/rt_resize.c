/**
 * rt_resize.c — 模拟"先渲染 480, 再放大到 800 并重画"的活窗口缩放序列。
 * 用于复现离屏画布在放大后空白/消失的 bug (canvas03/canvas04)。
 * 复用一个假驱动 + 纯软件路径, 与 render_canvas.c 相同的捕获逻辑。
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void fxtk_draw_all(void);
/* 文字/驱动符号桩 (与 render_canvas.c 相同) */
void fx_draw_text(int x,int y,const char*s){(void)x;(void)y;(void)s;}
void fx_draw_text_c(int x,int y,const char*s,fx_color_t fg,fx_color_t bg){(void)x;(void)y;(void)s;(void)fg;(void)bg;}
void fx_draw_text_c_n(int x,int y,const char*s,int n,fx_color_t fg,fx_color_t bg){(void)x;(void)y;(void)s;(void)n;(void)fg;(void)bg;}
int  fx_text_width(const char*s){(void)s;return 0;}
int  fx_text_width_n(const char*s,int n){(void)s;(void)n;return 0;}
void fxtk_draw_text_size(int size,int x,int y,const char*t,fx_color_t fg,fx_color_t bg){(void)size;(void)x;(void)y;(void)t;(void)fg;(void)bg;}
int  fxtk_text_width_size(int size,const char*t){(void)size;(void)t;return 0;}
void *fxtk_font_size(int size){(void)size;return NULL;}
int  fxtk_font_height(int size){(void)size;return 0;}
void fxtk_font_set_size(int size){(void)size;}
int  fxtk_fps(void){return 60;}
int  fxtk_shift_down(void){return 0;}
int  fxtk_right_click(int*x,int*y){(void)x;(void)y;return 0;}
int  fxtk_ui_scale(void){return 100;}

static uint32_t *s_fb=NULL; static int s_fbw=0,s_fbh=0;
static int gx0,gy0,gx1,gy1;
static int drv_init(void){return 0;}
static void drv_set_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){gx0=x0;gy0=y0;gx1=x1;gy1=y1;}
static void drv_push_pixels(const uint32_t*px,uint32_t n){int w=gx1-gx0+1;if(w<=0)w=1;for(uint32_t i=0;i<n;i++){int x=gx0+(int)(i%(uint32_t)w);int y=gy0+(int)(i/(uint32_t)w);if(x>=0&&x<s_fbw&&y>=0&&y<s_fbh)s_fb[y*s_fbw+x]=px[i];}}
static void drv_hold_begin(void){}
static void drv_hold_end(void){}
static int  drv_touch_read(int*x,int*y,int*p){(void)x;(void)y;(void)p;return 0;}
static int  drv_key_read(fx_keyev_t*ev){(void)ev;return 0;}
static void drv_clip_set(const char*s){(void)s;}
static const char *drv_clip_get(void){return "";}
static void drv_set_title(const char*s){(void)s;}
static int  drv_wheel_read(int*x,int*y,int*dy){(void)x;(void)y;(void)dy;return 0;}
static void drv_set_clip_rect(int x1,int y1,int x2,int y2){(void)x1;(void)y1;(void)x2;(void)y2;}
static void drv_blit_img(const uint32_t*px,int w,int h,int dx,int dy,int dw,int dh,int dark){(void)dark;if(!px||w<=0||h<=0||dw<=0||dh<=0)return;for(int ty=0;ty<dh;ty++){int sy=ty*h/dh;for(int tx=0;tx<dw;tx++){int sx=tx*w/dw;int x=dx+tx,y=dy+ty;if(x>=0&&x<s_fbw&&y>=0&&y<s_fbh)s_fb[y*s_fbw+x]=px[sy*w+sx];}}}

static fx_driver_t s_drv={
  .width=480,.height=272,.init=drv_init,.set_window=drv_set_window,.push_pixels=drv_push_pixels,
  .hold_begin=drv_hold_begin,.hold_end=drv_hold_end,.touch_read=drv_touch_read,.key_read=drv_key_read,
  .clip_set=drv_clip_set,.clip_get=drv_clip_get,.set_title=drv_set_title,.wheel_read=drv_wheel_read,
  .set_clip_rect=drv_set_clip_rect,.blit_img=drv_blit_img,
  .fill_rect=NULL,.draw_line=NULL,.fill_tri=NULL,.blit_img_rot=NULL,.blit_tex=NULL,
};
extern void app_init(void);
static void dump(const char*out){FILE*f=fopen(out,"wb");fprintf(f,"P6\n%d %d\n255\n",s_fbw,s_fbh);for(int i=0;i<s_fbw*s_fbh;i++){uint32_t c=s_fb[i];unsigned char r=(c>>16)&255,g=(c>>8)&255,b=c&255;fwrite(&r,1,1,f);fwrite(&g,1,1,f);fwrite(&b,1,1,f);}fclose(f);fprintf(stderr,"wrote %s (%dx%d)\n",out,s_fbw,s_fbh);}

int main(void){
  /* 初始 480x272 */
  s_drv.width=480;s_drv.height=272;s_fbw=480;s_fbh=272;s_fb=malloc(480u*272u*4u);
  fx_init(&s_drv); app_init(); fx_layout(); fxtk_draw_all(); dump("/tmp/rt_before.ppm");
  /* 模拟放大到 800x480 (不重跑 app_init, 只更新驱动尺寸+重画) */
  s_drv.width=800;s_drv.height=480;free(s_fb);s_fbw=800;s_fbh=480;s_fb=malloc(800u*480u*4u);
  fx_layout(); fxtk_draw_all(); dump("/tmp/rt_after.ppm");
  free(s_fb); return 0;
}
