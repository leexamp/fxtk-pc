/* fxtk_extra.c — 列表/下拉 终版: 弹层对象池(零分配/零布局抖动) + 不越界+滚动 */
#define _POSIX_C_SOURCE 200809L   /* 让 <string.h> 暴露 strdup (严格 -std 下会隐式声明) */
#include "fxtk.h"
#include "fxtk_tokens.h"
#include "fxtk_internal.h"
#include "fxtk_desktop.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if FXTK_WIDGET_LIST || FXTK_WIDGET_DROP
#define EX_MAXL 64
typedef struct {
    fx_widget_t *w;
    char *items[EX_MAXL];
    int n, sel, scroll, row_h, was_pr, wacc, hov;
    void (*cb)(fx_widget_t *, void *);
    fx_widget_t *pop, *owner;
} ex_slot_t;
static fx_color_t ex_darken(fx_color_t c)
{   int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
    return (fx_color_t)(((r*3/4)<<16)|((g*3/4)<<8)|(b*3/4)); }
static ex_slot_t s_ex[8];
#if FXTK_WIDGET_DROP
static void ex_close_pop(ex_slot_t *o);   /* v2.3.1: 前向声明 (定义在下方 DROP 段, fx_list_clear 需要提前用) */
#endif
static ex_slot_t *ex_get(fx_widget_t *w){ for(int i=0;i<8;i++) if(s_ex[i].w==w) return &s_ex[i]; return 0; }
static ex_slot_t *ex_new(fx_widget_t *w){ for(int i=0;i<8;i++) if(!s_ex[i].w){ memset(&s_ex[i],0,sizeof(ex_slot_t)); s_ex[i].w=w; s_ex[i].row_h=22; s_ex[i].sel=-1; return &s_ex[i]; } return 0; }
static int ex_rh(ex_slot_t *s){ int rh=s->row_h*fxtk_ui_scale()/100;
if (rh<12)rh=12;
if (!getenv("FXTK_COMPACT")&&rh>32)rh=32; return rh; }
#if FXTK_WIDGET_LIST
static void ex_draw_list(ex_slot_t *s, int cw, int ch)
{
    /* P5 审美迭代 5/9(列表/下拉): 外框与行高亮原先都是硬直角, 且选中行从 x=1 铺到 cw-2
     * (紧贴外框, 视觉上"顶格"); 与其它控件的圆角语言也不一致。改为圆角外框 + 内缩的圆角行高亮,
     * 调用次数不变(填充/描边各 1 次, 只是换成圆角版本)。 */
    int lr = FX_TOK_RADIUS_S;
    { if (lr > ch/2) lr = ch/2; if (lr > cw/2) lr = cw/2; }
    fx_set_color(s->w?s->w->bg:FX_WHITE); fx_fill_rect_round(0,0,cw-1,ch-1,lr);
    fx_set_color(FX_TOK_LIST_EDGE);       fx_draw_rect_round(0,0,cw-1,ch-1,lr);
    int vis=(ch-2)/ex_rh(s);
    if (vis>s->n-s->scroll) vis=s->n-s->scroll;
    for(int i=0;i<vis;i++){
        int idx=s->scroll+i, y=1+i*ex_rh(s);
        if (!s->items[idx]) continue;
        int mx,my,mp; fx_touch_state(&mx,&my,&mp);
        int hov=-1; { int lx=mx-s->w->x1, ly=my-s->w->y1;
        if (lx>=0&&lx<cw&&ly>=1&&ly<ch-1) hov=(ly-1)/ex_rh(s)+s->scroll; }
        /* 行高亮: 内缩 2px 的圆角胶囊, 不再顶格贴住外框(与标签页选中态同一套语言) */
        int hy0 = y, hy1 = y + ex_rh(s) - 1;
        int sel_or_hov = (idx==s->sel) ? 1 : (idx==hov ? 2 : 0);
        if (sel_or_hov) {
            int hr = FX_TOK_RADIUS_S; int hh = hy1 - hy0 + 1;
            if (hr > hh/2 - 1) hr = hh/2 - 1; if (hr < 2) hr = 2;
            fx_set_color(sel_or_hov==1 ? FX_TOK_PRIMARY : FX_TOK_LIST_HOVER);
            fx_fill_rect_round(3, hy0 + 1, cw - 6, hy1 - 1, hr);
        }
        fx_draw_text_c(FX_TOK_TEXT_PAD_X, y+3, s->items[idx],
                       idx==s->sel ? FX_TOK_ON_PRIMARY : FX_TOK_TEXT,
                       idx==s->sel ? FX_TOK_PRIMARY   : (idx==hov ? FX_TOK_LIST_HOVER : (s->w?s->w->bg:FX_WHITE)));
    }
    { int mx,my,mp; fx_touch_state(&mx,&my,&mp); int hv=-1; int lx=mx-s->w->x1, ly=my-s->w->y1;
    if (lx>=0&&lx<cw&&ly>=1&&ly<ch-1) hv=(ly-1)/ex_rh(s)+s->scroll;
    if (hv!=s->hov){ s->hov=hv; fx_repaint_rect(s->w->x1,s->w->y1,s->w->x2,s->w->y2); } }
    int total=s->n*ex_rh(s);
    if (total>ch-2){
        int th=(ch-2)*(ch-2)/total;
        if (th<12)th=12;
        int ty=1+(long)s->scroll*ex_rh(s)*(ch-2-th)/(total-(ch-2));
        fx_set_color(FX_TOK_SCROLL_TRACK); fx_fill_rect(cw-4,1,cw-2,ch-2);
        fx_set_color(FX_TOK_SCROLL_THUMB); fx_fill_rect(cw-4,ty,cw-2,ty+th);
    }
}
static void ex_list_cb(fx_widget_t *w, void *ud)
{
    ex_slot_t *s=ex_get(w);
    if (!s)return;
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    int maxs=s->n-(ch-2)/ex_rh(s);
    if (maxs<0)maxs=0;
    { int dy=fx_wheel_take(w); s->wacc+=dy; int dr=s->wacc/ex_rh(s); s->wacc-=dr*ex_rh(s); s->scroll-=dr; }
    if(s->scroll<0)s->scroll=0;
    if (s->scroll>maxs)s->scroll=maxs;
    int mx,my,mp; fx_touch_state(&mx,&my,&mp);
    int pr=(fx_pressed()==w);
    if (s->was_pr&&!pr&&mx>=x1&&mx<=x2&&my>=y1&&my<=y2){
        int i=(my-y1-1)/ex_rh(s)+s->scroll;
        if (i>=0&&i<s->n){ s->sel=i;
        if (s->cb)s->cb(w,(void*)(intptr_t)i); }
    }
    s->was_pr=pr;
    ex_draw_list(s,cw,ch);
}
fx_widget_t *fx_list_new_p(const char*r1,const char*r2,int pg)
{
    fx_widget_t *w=(pg>=0)?fx_canvas_new(pixel(r1,r2),page(pg),color(FX_WHITE),anim(1))
                         :fx_canvas_new(pixel(r1,r2),color(FX_WHITE),anim(1));
    ex_slot_t *s=ex_new(w);
    if (s)fx_set_cb(w,ex_list_cb,0);
    return w;
}
fx_widget_t *fx_list_new(const char*r1,const char*r2){return fx_list_new_p(r1,r2,-1);}
void fx_list_add(fx_widget_t *w,const char*t){ ex_slot_t*s=ex_get(w);
if (!s||s->n>=EX_MAXL)return; s->items[s->n]=strdup(t);
if (s->sel<0)s->sel=0; s->n++; }
void fx_list_set_cb(fx_widget_t *w,void(*cb)(fx_widget_t*,void*)){ ex_slot_t*s=ex_get(w);
if (s)s->cb=cb; }
int fx_list_sel(fx_widget_t *w){ ex_slot_t*s=ex_get(w); return s?s->sel:-1; }
void fx_list_clear(fx_widget_t *w){ ex_slot_t*s=ex_get(w);
if (!s)return;
#if FXTK_WIDGET_DROP
if (s->pop) ex_close_pop(s);   /* v2.3.1: 弹层条目是别名指针(非拷贝), 先关弹层再 free, 防 UAF */
#endif
for(int i=0;i<s->n;i++)free(s->items[i]); s->n=0; s->sel=-1; }
/* ---------- 下拉: 池化弹层 (建一次, 复用, 开合零布局) ---------- */
#endif
#if FXTK_WIDGET_DROP
static fx_widget_t *s_pop=NULL;
static void ex_pop_cb(fx_widget_t *w, void *ud);
static void ex_ensure_pop(void){
    if(s_pop)return;
    s_pop=fx_canvas_new(pixel("-2000,-2000","-1900,-1900"),color(FX_WHITE),anim(1));
    if (s_pop){ ex_new(s_pop); fx_set_cb(s_pop,ex_pop_cb,0); s_pop->pos_mode=FX_POS_FIXED; s_pop->page=-1; }
}
static void ex_close_pop(ex_slot_t *o)
{
    if(!o->pop)return;
    fx_widget_t *p=o->pop;
    int x1,y1,x2,y2; fx_widget_rect(p,&x1,&y1,&x2,&y2);
    ex_slot_t *ps=ex_get(p);
    if (ps)ps->owner=0;
    o->pop=0;
    p->page=-1;                                   /* 离页: 不画不命中 */
    fx_widget_set_rect(p,-2000,-2000,-1900,-1900);/* 池化: 移屏外, 不销毁 */
    fx_repaint_rect(x1,y1,x2,y2);
    fx_widget_rect(o->w,&x1,&y1,&x2,&y2);
    fx_repaint_rect(x1,y1,x2,y2);
}
static void ex_pop_cb(fx_widget_t *w, void *ud)
{
    ex_slot_t *s=ex_get(w);
    if (!s)return;
    ex_slot_t *o=ex_get(s->owner);
    if (!o){   /* v2.3.1: 属主已删除/槽已复用 — 旧代码 ex_close_pop(s) 因弹层槽 pop==0 空转, 会继续画悬垂条目 */
        s->owner=0; s->n=0; s->sel=-1;
        w->page=-1;
        fx_widget_set_rect(w,-2000,-2000,-1900,-1900);
        fx_repaint();
        return;
    }
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    int mx,my,mp; fx_touch_state(&mx,&my,&mp);
    int inside=mx>=x1&&mx<=x2&&my>=y1&&my<=y2;
    int pr=(fx_pressed()==w);
    if (mp&&!inside&&!pr){ ex_close_pop(o); return; }
    int maxs=s->n-(ch-2)/ex_rh(s);
    if (maxs<0)maxs=0;
    { int dy=fx_wheel_take(w); s->wacc+=dy; int dr=s->wacc/ex_rh(s); s->wacc-=dr*ex_rh(s); s->scroll-=dr; }
    if(s->scroll<0)s->scroll=0;
    if (s->scroll>maxs)s->scroll=maxs;
    if (s->was_pr&&!pr&&inside){
        int i=(my-y1-1)/ex_rh(s)+s->scroll;
        if (i>=0&&i<s->n){ o->sel=i; void(*cb)(fx_widget_t*,void*)=o->cb; ex_close_pop(o);
        if (cb)cb(o->w,(void*)(intptr_t)i); return; }
    }
    s->was_pr=pr;
    ex_draw_list(s,cw,ch);
}
static void ex_drop_cb(fx_widget_t *w, void *ud)
{
    ex_slot_t *s=ex_get(w);
    if (!s)return;
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    int mx,my,mp; fx_touch_state(&mx,&my,&mp);
    int pr=(fx_pressed()==w);
    if (s->was_pr&&!pr){
        if(s->pop){ ex_close_pop(s); }
        else{
            ex_ensure_pop();
            int H=s->n*ex_rh(s)+2;
            int down_avail=(fx_height())-4-(y2+2);
            int up_avail=y1-6;
            int up=0;
            if (H>down_avail&&up_avail>down_avail)up=1;
            int avail=up?up_avail:down_avail;
            if (H>avail)H=avail;
            if (H<ex_rh(s)+2)H=ex_rh(s)+2;
            if (H>s->n*ch)H=s->n*ch;
            int bh=ex_rh(s)+10;
            if (bh>ch)bh=ch; int yb=y1+bh; int py1=up?y1-2-H:yb+2;
            ex_slot_t *ps=ex_get(s_pop);
            if (ps){
                ps->owner=w;
                for(int i=0;i<s->n;i++)ps->items[i]=s->items[i];
                ps->n=s->n; ps->sel=s->sel; ps->row_h=s->row_h;
                ps->scroll=s->sel>0?s->sel-1:0;
                s_pop->page=w->page;
                fx_widget_set_rect(s_pop,x1,py1,x2,py1+H-1);
                s->pop=s_pop;
            }
        }
    }
    s->was_pr=pr;
    { int nbh=ex_rh(s)+10;
    if (ch>nbh){ fx_set_color(FX_RGB(245,245,245)); fx_fill_rect(0,0,cw-1,ch-1); fx_widget_set_rect(w,x1,y1,x2,y1+nbh-1); y2=y1+nbh-1; ch=nbh; } }
    fx_set_color(FX_RGB(245,245,245)); fx_fill_rect(0,0,cw-1,ch-1);   /* 先整块擦净, 消除高分辨率下巴 */
    int sz=(w->lines>0)?w->lines:14; int bh=ch; fx_set_color(w->bg); fx_fill_rect(0,0,cw-1,bh-1);
    fx_set_color(ex_darken(w->bg)); fx_draw_rect(0,0,cw-1,bh-1);
    const char *t=(s->sel>=0&&s->items[s->sel])?s->items[s->sel]:"—";
    fxtk_draw_text_size(sz,6,(bh-sz)/2,t,w->fg,w->bg);
    fx_set_color(w->fg);
    for(int i=0;i<5;i++)fx_draw_hline(cw-12+i,cw-2-i,bh/2-2+i);
}
fx_widget_t *fx_drop_new_p(const char*r1,const char*r2,int pg)
{
    ex_ensure_pop();
    fx_widget_t *w=(pg>=0)?fx_canvas_new(pixel(r1,r2),page(pg),color(FX_RGB(250,250,250)),fgcolor(FX_RGB(40,40,40)),anim(1))
                         :fx_canvas_new(pixel(r1,r2),color(FX_RGB(250,250,250)),fgcolor(FX_RGB(40,40,40)),anim(1));
    w->bg=FX_RGB(245,245,245); w->border=0;   /* 底盘与页面同色, 高分辨率无下巴 */
    ex_slot_t *s=ex_new(w);
    if (s)fx_set_cb(w,ex_drop_cb,0);
    return w;
}
fx_widget_t *fx_drop_new(const char*r1,const char*r2){return fx_drop_new_p(r1,r2,-1);}
void fx_drop_add(fx_widget_t *w,const char*t){ fx_list_add(w,t); }
#endif
#endif

/* v2.3.1: 控件删除/系统重置时清掉按指针索引的槽位。
 * 控件池地址复用会让新控件继承旧列表条目(悬垂/泄漏); s_pop 缓存指针在 fx_init 重置池后同样悬垂。 */
void fxtk_extra_forget(const fx_widget_t *w)
{
#if FXTK_WIDGET_LIST || FXTK_WIDGET_DROP
    if (!w) return;
    for (int i = 0; i < 8; i++)
        if (s_ex[i].w == w) memset(&s_ex[i], 0, sizeof(ex_slot_t));
#endif
}
void fxtk_extra_reset(void)
{
#if FXTK_WIDGET_LIST || FXTK_WIDGET_DROP
    memset(s_ex, 0, sizeof(s_ex));
#if FXTK_WIDGET_DROP
    s_pop = NULL;
#endif
#endif
}
void fx_set_fontsize(fx_widget_t *w,int size){ if(!w)return; w->lines=(int16_t)size; fx_repaint(); }
void fx_set_align(fx_widget_t *w,int a){ if(!w)return; w->rows=(int16_t)a; fx_repaint(); }
