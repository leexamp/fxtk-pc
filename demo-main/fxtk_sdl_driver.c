/* fxtk_sdl_driver.c — v2.0 终版: 矩形SDL自批 + 折线/三角顶点批 + 图片GPU缩放 + 持久target */
#include "fxtk.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define INITIAL_WIDTH 480
#define INITIAL_HEIGHT 272
fx_driver_t fx_sdl_driver;
static int s_width=INITIAL_WIDTH,s_height=INITIAL_HEIGHT;
static int win_w=INITIAL_WIDTH,win_h=INITIAL_HEIGHT;
static int cap_max=0;
static SDL_Window *window=NULL; static SDL_Renderer *renderer=NULL;
static SDL_Texture *target=NULL; static SDL_Texture *scratch=NULL;
static int scr_w=0,scr_h=0;
static uint16_t cur_x0,cur_y0,cur_w; static uint32_t cur_idx=0; static int cur_pending=0;
static int s_fps=0,s_fps_n=0;
static int stat_vb=0, stat_px=0; static uint32_t s_fps_t0=0;
static int g_stat=0;   /* 调试统计打印开关 (FXTK_STAT=1) */
int fxtk_fps(void){return s_fps;}
static inline uint32_t rgba_from_rgb(uint32_t c){ return 0xFF000000u|c; }   /* 0xRRGGBB → ARGB */
#define VB_MAX 260000
static SDL_Vertex vb[VB_MAX]; static int vb_n=0;
static void flush_batch(void); static void vb_push(float,float,SDL_Color); static void vb_line(int,int,int,int,SDL_Color); static void vb_tri(int,int,int,int,int,int,SDL_Color);
static void flush_batch(void){ if(!vb_n)return; SDL_RenderGeometry(renderer,NULL,vb,vb_n,NULL,0); vb_n=0; }
static void vb_push(float x,float y,SDL_Color col){ stat_vb++; vb[vb_n].position.x=x;vb[vb_n].position.y=y;vb[vb_n].color=col;vb[vb_n].tex_coord.x=0;vb[vb_n].tex_coord.y=0;vb_n++; }
static void vb_line(int x1,int y1,int x2,int y2,SDL_Color col){ float dx=(float)(x2-x1),dy=(float)(y2-y1); float len=sqrtf(dx*dx+dy*dy);
if (len<0.001f)len=1; float nx=-dy/len*0.5f,ny=dx/len*0.5f;
if (vb_n+6>VB_MAX)flush_batch(); vb_push(x1+nx,y1+ny,col);vb_push(x1-nx,y1-ny,col);vb_push(x2-nx,y2-ny,col);vb_push(x1+nx,y1+ny,col);vb_push(x2-nx,y2-ny,col);vb_push(x2+nx,y2+ny,col); }
static void vb_tri(int x1,int y1,int x2,int y2,int x3,int y3,SDL_Color col){ if(vb_n+3>VB_MAX)flush_batch(); vb_push(x1,y1,col);vb_push(x2,y2,col);vb_push(x3,y3,col); }
static uint32_t *fb_rgba=NULL; static int fb_w=0,fb_h=0;
static int d_x0=0,d_y0=0,d_x1=0,d_y1=0,d_pending=0;
static void fb_ensure(void){ if(fb_w!=s_width||fb_h!=s_height){ free(fb_rgba); fb_rgba=(uint32_t*)malloc((size_t)s_width*s_height*4); fb_w=s_width;fb_h=s_height; d_pending=0; } }
static void flush_pixels(void)
{
    if(!d_pending){cur_pending=0;cur_idx=0;return;}
    flush_batch();
    if (!scratch||scr_w!=fb_w||scr_h!=fb_h){ if(scratch)SDL_DestroyTexture(scratch); scratch=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,fb_w,fb_h); SDL_SetTextureBlendMode(scratch,SDL_BLENDMODE_BLEND); scr_w=fb_w;scr_h=fb_h; }
    if(!scratch){d_pending=0;return;}
    int w=d_x1-d_x0+1,h=d_y1-d_y0+1;
    if (w<1||h<1){d_pending=0;return;}
    SDL_Rect r={d_x0,d_y0,w,h};
    SDL_UpdateTexture(scratch,&r,fb_rgba+(size_t)d_y0*fb_w+d_x0,fb_w*4);   /* 整帧单次上传 */
    SDL_RenderCopy(renderer,scratch,&r,&r);
    for(int yy=d_y0; yy<=d_y1; yy++) memset(fb_rgba+(size_t)yy*fb_w+d_x0, 0, (size_t)w*4);   /* 清脏区防残影 */
    d_pending=0;cur_pending=0;cur_idx=0;
}
static void sdl_set_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){ cur_x0=x0;cur_y0=y0;cur_w=(uint16_t)(x1-x0+1);cur_idx=0;cur_pending=1; }
static void sdl_push_pixels(const uint32_t *px,uint32_t n){ stat_px+=n; fb_ensure();
if (!fb_rgba)return;
    uint32_t col=cur_idx%cur_w, row=cur_idx/cur_w;   /* 增量推进, 免每像素除法 */
    for(uint32_t i=0;i<n;i++){
        uint32_t x=cur_x0+col, y=cur_y0+row;
        if ((int)x<fb_w&&(int)y<fb_h){ fb_rgba[y*fb_w+x]=rgba_from_rgb(px[i]);
        if (!d_pending){d_x0=d_x1=(int)x;d_y0=d_y1=(int)y;d_pending=1;}
            else{ if((int)x<d_x0)d_x0=x;
            if ((int)x>d_x1)d_x1=x;
            if ((int)y<d_y0)d_y0=y;
            if ((int)y>d_y1)d_y1=y; } }
        if(++col==cur_w){col=0;row++;}
    }
    cur_idx+=n;
}
static void sdl_fill_rect(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint32_t color)
{
    if(x0>x1||y0>y1)return;
    SDL_SetRenderDrawColor(renderer,(Uint8)((color>>16)&0xFF),(Uint8)((color>>8)&0xFF),(Uint8)(color&0xFF),255);
    SDL_Rect rc={x0,y0,(int)(x1-x0+1),(int)(y1-y0+1)};
    SDL_RenderFillRect(renderer,&rc);
}
static void sdl_draw_line(int x1,int y1,int x2,int y2,uint32_t color){ flush_pixels(); SDL_Color col={(Uint8)((color>>16)&0xFF),(Uint8)((color>>8)&0xFF),(Uint8)(color&0xFF),255}; vb_line(x1,y1,x2,y2,col); }
static void sdl_fill_tri(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color){ flush_pixels(); SDL_Color col={(Uint8)((color>>16)&0xFF),(Uint8)((color>>8)&0xFF),(Uint8)(color&0xFF),255}; vb_tri(x1,y1,x2,y2,x3,y3,col); }
static SDL_Texture *img_tex=NULL; static int img_tw=0,img_th=0; static uint32_t *img_tmp=NULL; static int img_cap=0;
static void sdl_blit_img(const uint32_t *px,int w,int h,int dx,int dy,int dw,int dh,int dark)
{
    if(dw<=0||dh<=0||w<=0||h<=0||!px)return;
    flush_pixels(); flush_batch();
    if (!img_tex||img_tw!=w||img_th!=h){ if(img_tex)SDL_DestroyTexture(img_tex); img_tex=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h); img_tw=w;img_th=h;
    if (!img_tex)return; SDL_SetTextureBlendMode(img_tex,SDL_BLENDMODE_NONE); }
    if(w*h>img_cap){ uint32_t*nb=(uint32_t*)realloc(img_tmp,(size_t)w*h*4);
    if (!nb)return; img_tmp=nb;img_cap=w*h; }
    for(int i=0;i<w*h;i++)img_tmp[i]=rgba_from_rgb(px[i]);
    SDL_UpdateTexture(img_tex,NULL,img_tmp,w*4);
    if (dark)SDL_SetTextureColorMod(img_tex,128,128,128);
    SDL_Rect dst={dx,dy,dw,dh}; SDL_RenderCopy(renderer,img_tex,NULL,&dst);
    if (dark)SDL_SetTextureColorMod(img_tex,255,255,255);
}
static void sdl_blit_tex(void *t,int sx,int sy,int sw,int sh,int dx,int dy)
{
    flush_pixels(); flush_batch();
    SDL_Rect src={sx,sy,sw,sh}, dst={dx,dy,sw,sh};
    SDL_RenderCopy(renderer,(SDL_Texture*)t,&src,&dst);
}
#define ROT_N 4
static struct{ const uint32_t*px; int w,h; SDL_Texture*t; } rot_cache[ROT_N];
static void sdl_blit_img_rot(const uint32_t *px,int w,int h,int cx,int cy,int dw,int dh,double ang)
{
    if(!px||w<=0||h<=0||dw<=0||dh<=0)return;
    flush_pixels(); flush_batch();
    int slot=-1;
    for(int i=0;i<ROT_N;i++) if(rot_cache[i].t&&rot_cache[i].px==px&&rot_cache[i].w==w&&rot_cache[i].h==h){slot=i;break;}
    if(slot<0){
        for(int i=0;i<ROT_N;i++) if(!rot_cache[i].t){slot=i;break;}
        if(slot<0){ SDL_DestroyTexture(rot_cache[0].t); rot_cache[0].t=NULL; rot_cache[0].px=NULL; slot=0; }
        if(w*h>img_cap){ uint32_t*nb=(uint32_t*)realloc(img_tmp,(size_t)w*h*4);
        if (!nb)return; img_tmp=nb;img_cap=w*h; }
        for(int i=0;i<w*h;i++)img_tmp[i]=rgba_from_rgb(px[i]);
        SDL_Texture*t=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);
        if (!t)return;
        SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(t,NULL,img_tmp,w*4);
        rot_cache[slot].px=px;rot_cache[slot].w=w;rot_cache[slot].h=h;rot_cache[slot].t=t;
    }
    SDL_Rect dst={cx-dw/2,cy-dh/2,dw,dh};
    SDL_RenderCopyEx(renderer,rot_cache[slot].t,NULL,&dst,-ang,NULL,SDL_FLIP_NONE);   /* 硬件旋转+缩放 */
}
static void sdl_set_clip_rect(int x1,int y1,int x2,int y2){ flush_batch();
if (x1>30000||x1>x2||y1>y2){SDL_RenderSetClipRect(renderer,NULL);return;} SDL_Rect r={x1,y1,x2-x1+1,y2-y1+1}; SDL_RenderSetClipRect(renderer,&r); }
/* 与核心库当前背景色一致, 避免清屏色与控件底色有可见色差 */
static void clear_color_from_bg(void)
{
    extern fx_color_t fx_get_bg(void);
    fx_color_t c = fx_get_bg();
    SDL_SetRenderDrawColor(renderer,
        (Uint8)((c>>16)&0xFF), (Uint8)((c>>8)&0xFF), (Uint8)(c&0xFF), 255);
}
static void target_create(void)
{
    if(target)SDL_DestroyTexture(target);
    target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,s_width,s_height);
    if (target){ SDL_SetRenderTarget(renderer,target); clear_color_from_bg(); SDL_RenderClear(renderer); }
    fx_repaint();
}
static int sdl_init(void)
{
    g_stat = getenv("FXTK_STAT") ? 1 : 0;   /* 调试统计默认关闭, FXTK_STAT=1 开启 */
    const char *mw=getenv("FXTK_MAXW");
    if (mw)cap_max=atoi(mw);
    if (SDL_Init(SDL_INIT_VIDEO)<0){printf("SDL Init failed: %s\n",SDL_GetError());return -1;}
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    window=SDL_CreateWindow("fxtk v2.0 · 立即模式GPU",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,INITIAL_WIDTH,INITIAL_HEIGHT,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    if (window)SDL_SetWindowMinimumSize(window,240,136);
    if (window && cap_max>0) SDL_SetWindowMaximumSize(window,cap_max,cap_max);   /* FXTK_MAXW: 限窗口上限, 防大窗口撑爆窗口尺寸表面/GL 目标 */
    if(!window){printf("Window failed: %s\n",SDL_GetError());return -1;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|(getenv("FXTK_BENCH")?0:SDL_RENDERER_PRESENTVSYNC));
    /* 帧节奏由主循环精确同步, vsync 只作防撕裂; 关闭时由 sdl_update_screen 自同步 */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, getenv("FXTK_BENCH") ? "0" : "1");
    if (!renderer)renderer=SDL_CreateRenderer(window,-1,0);
    if (!renderer){printf("Renderer failed: %s\n",SDL_GetError());return -1;}
    SDL_RendererInfo ri; SDL_GetRendererInfo(renderer,&ri);
    printf("[drv] backend: %s %s\n",ri.name,(ri.flags&SDL_RENDERER_ACCELERATED)?"(GPU加速)":"(软件回退!)");
    return 0;
}
static void sdl_apply_size(int w,int h)
{
    if(w<160)w=160;
    if (h<120)h=120;
    win_w=w;win_h=h;
    if (w==s_width&&h==s_height)return;
    s_width=w;s_height=h;
    fx_sdl_driver.width=(uint16_t)s_width;fx_sdl_driver.height=(uint16_t)s_height;
    fb_w=0; fb_h=0;   /* 置 0 让 fb_ensure 按新尺寸重分配 fb_rgba (否则 resize 后软件像素全被丢弃而空白) */
    if(fb_rgba){free(fb_rgba);fb_rgba=0;} d_pending=0;
    if (scratch){SDL_DestroyTexture(scratch);scratch=0;scr_w=0;scr_h=0;}
    target_create();
    fx_layout();fx_repaint();fx_repaint();
}
static int mouse_pressed=0,mouse_x=0,mouse_y=0;
static int g_lclick=0;
static int sdl_touch_read(int*x,int*y,int*p){*x=mouse_x*s_width/win_w;*y=mouse_y*s_height/win_h;*p=mouse_pressed;
if (!*p&&g_lclick){g_lclick=0;*p=1;} return 1;}
#define FX_KEYQUEUE 64
static fx_keyev_t g_kq[FX_KEYQUEUE];static int g_kq_n=0;
static int sdl_key_read(fx_keyev_t*ev);
static float g_wheel_pix=0;
static int g_uicap=160;
void fxtk_set_ui_scale_cap(int p){ if(p<100)p=100;
if (p>400)p=400; g_uicap=p; }
static int g_rc=0,g_rx=0,g_ry=0;
int fxtk_shift_down(void){ return (SDL_GetModState()&KMOD_SHIFT)?1:0; }
int fxtk_right_click(int*x,int*y){ if(!g_rc)return 0; g_rc=0; *x=g_rx;*y=g_ry; return 1; }
static int sdl_wheel_read(int*x,int*y,int*dy);
static void sdl_set_title(const char*s); static void sdl_clip_set(const char*s); static const char*sdl_clip_get(void);
static uint64_t last_frame=0; static double acc_frame=0,acc_present=0; static int acc_n=0;
void sdl_update_screen(void)
{
    if (!renderer || !target) return;
    uint64_t _now=SDL_GetPerformanceCounter();
    if (last_frame)acc_frame+=(double)(_now-last_frame)*1000.0/SDL_GetPerformanceFrequency();
    last_frame=_now;
    uint64_t _p0=SDL_GetPerformanceCounter();
    flush_pixels(); flush_batch();
    s_fps_n++; uint32_t now=SDL_GetTicks();
    if (!s_fps_t0)s_fps_t0=now;
    if (now-s_fps_t0>=1000){s_fps=s_fps_n*1000/(int)(now-s_fps_t0);s_fps_n=0;s_fps_t0=now;
    if (g_stat)printf("[stat] vb=%d px=%d\n", stat_vb, stat_px);}
    stat_vb=0; stat_px=0;
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderSetClipRect(renderer,NULL);
    clear_color_from_bg(); SDL_RenderClear(renderer);   /* 清屏色跟随核心背景, 无色差 */
    SDL_RenderCopy(renderer,target,NULL,NULL);
    SDL_RenderPresent(renderer);
    SDL_SetRenderTarget(renderer,target);
    SDL_RenderSetClipRect(renderer,NULL);   /* 下一帧从全画布开始, 防clip残留 */
    if(g_stat){ uint64_t _p1=SDL_GetPerformanceCounter(); acc_present+=(double)(_p1-_p0)*1000.0/SDL_GetPerformanceFrequency(); acc_n++;
    if (acc_n>=60){ printf("[perf] frame=%.2fms present=%.2fms draw=%.2fms\n", acc_frame/acc_n, acc_present/acc_n, (acc_frame-acc_present)/acc_n); acc_frame=acc_present=0;acc_n=0; } }
}
void sdl_handle_events(void)
{
    SDL_Event e;
    while(SDL_PollEvent(&e)){
        if(e.type==SDL_QUIT)exit(0);
        else if(e.type==SDL_TEXTINPUT){ if(g_kq_n<FX_KEYQUEUE){fx_keyev_t ev={{0},0,1}; strncpy(ev.utf8,e.text.text,sizeof(ev.utf8)-1);ev.utf8[sizeof(ev.utf8)-1]=0;g_kq[g_kq_n++]=ev;} }
        else if(e.type==SDL_KEYDOWN&&!e.key.repeat){
            int k=0;int mod=((SDL_GetModState()&KMOD_CTRL)?1:0)|((SDL_GetModState()&KMOD_SHIFT)?2:0);
            switch(e.key.keysym.sym){
                case SDLK_BACKSPACE: k=FX_KEY_BACKSPACE; break;
                case SDLK_RETURN:    k=FX_KEY_RETURN;    break;
                case SDLK_ESCAPE:    k=FX_KEY_ESCAPE;    break;
                case SDLK_LEFT:      k=FX_KEY_LEFT;      break;
                case SDLK_RIGHT:     k=FX_KEY_RIGHT;     break;
                case SDLK_HOME:      k=FX_KEY_HOME;      break;
                case SDLK_END:       k=FX_KEY_END;       break;
                case SDLK_UP:        k=FX_KEY_UP;        break;
                case SDLK_DOWN:      k=FX_KEY_DOWN;      break;
                case SDLK_DELETE:    k=FX_KEY_DELETE;    break;
                default: break;
            }
            if(k&&g_kq_n<FX_KEYQUEUE){fx_keyev_t ev={{0},k,1,mod};g_kq[g_kq_n++]=ev;}
            else if(mod&&g_kq_n<FX_KEYQUEUE&&(e.key.keysym.sym==SDLK_c||e.key.keysym.sym==SDLK_v||e.key.keysym.sym==SDLK_x||e.key.keysym.sym==SDLK_a)){fx_keyev_t ev={{0},0,1,mod};ev.utf8[0]=(char)e.key.keysym.sym;g_kq[g_kq_n++]=ev;}}
        else if(e.type==SDL_WINDOWEVENT){ if(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||e.window.event==SDL_WINDOWEVENT_RESIZED)sdl_apply_size(e.window.data1,e.window.data2); }
        else if(e.type==SDL_MOUSEBUTTONDOWN){
            mouse_x=e.button.x;mouse_y=e.button.y;mouse_pressed=1;
            if (e.button.button==SDL_BUTTON_LEFT)g_lclick=1;
            else if(e.button.button==SDL_BUTTON_RIGHT){g_rc=1;g_rx=e.button.x*s_width/win_w;g_ry=e.button.y*s_height/win_h;}
        }
        else if(e.type==SDL_MOUSEBUTTONUP){mouse_x=e.button.x;mouse_y=e.button.y;mouse_pressed=0;}
        else if(e.type==SDL_MOUSEWHEEL){mouse_x=e.wheel.mouseX;mouse_y=e.wheel.mouseY;g_wheel_pix+=e.wheel.preciseY*24.0f;   /* 滚轮上=dy+, 内容上滚 */}
        else if(e.type==SDL_MOUSEMOTION){mouse_x=e.motion.x;mouse_y=e.motion.y;}
    }
}
fx_driver_t fx_sdl_driver={
    .width=INITIAL_WIDTH,.height=INITIAL_HEIGHT,
    .init=sdl_init,.set_window=sdl_set_window,.push_pixels=sdl_push_pixels,
    .fill_rect=sdl_fill_rect,.touch_read=sdl_touch_read,.key_read=sdl_key_read,
    .clip_set=sdl_clip_set,.clip_get=sdl_clip_get,.wheel_read=sdl_wheel_read,
    .set_title=sdl_set_title,.blit_img=sdl_blit_img,.blit_tex=sdl_blit_tex,.blit_img_rot=sdl_blit_img_rot,.set_clip_rect=sdl_set_clip_rect,
    .fill_tri=sdl_fill_tri,.draw_line=sdl_draw_line,
};
int sdl_get_width(void){return s_width;}
int sdl_get_height(void){return s_height;}
void sdl_first_target(void){ if(renderer&&!target){target_create();SDL_SetRenderTarget(renderer,target);} }
static int g_textinput_on=0;
static int sdl_key_read(fx_keyev_t*ev)
{
    if(!g_textinput_on){SDL_StartTextInput();g_textinput_on=1;}
    if(!g_kq_n)return 0;
    *ev=g_kq[0];
    memmove(&g_kq[0],&g_kq[1],sizeof(fx_keyev_t)*(size_t)(--g_kq_n));
    return 1;
}
static char g_clip[4096];
static void sdl_clip_set(const char*s){SDL_SetClipboardText(s?s:"");}
static const char*sdl_clip_get(void)
{
    if(!SDL_HasClipboardText())return "";
    char*t=SDL_GetClipboardText();
    if (!t)return "";
    strncpy(g_clip,t,4095);g_clip[4095]=0;SDL_free(t);
    return g_clip;
}
static int sdl_wheel_read(int*x,int*y,int*dy)
{
    *x=mouse_x*s_width/win_w;*y=mouse_y*s_height/win_h;
    if (g_wheel_pix>-1.0f&&g_wheel_pix<1.0f)return 0;
    int v=(int)g_wheel_pix; g_wheel_pix-=v; *dy=v; return 1;
}
static void sdl_set_title(const char*s){if(window)SDL_SetWindowTitle(window,s);}
SDL_Renderer *fxtk_get_sdl_renderer(void) { return renderer; }
void fxtk_flush_driver_batch(void) { flush_pixels(); flush_batch(); }
int fxtk_ui_scale(void)
{
    extern int fxtk_drv_width(void);
    int w=fxtk_drv_width();
    int s=w>0?(w*100)/480:100;
    if (s<100)s=100;
    if (!getenv("FXTK_COMPACT") && s>g_uicap) s=g_uicap;   /* 大屏默认限1.6x; FXTK_COMPACT=1才全比例 */
    return s;
}
