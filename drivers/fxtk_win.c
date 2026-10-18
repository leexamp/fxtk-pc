/* fxtk_win.c —— 多窗口能力 façade: 只做后端分发, 不含任何平台/图形 API */
#include "fxtk_win.h"
#include <string.h>

static const fxtk_win_backend *s_b = 0;

void fxtk_win_backend_set(const fxtk_win_backend *b) { s_b = b; }
const fxtk_win_backend *fxtk_win_backend_get(void) { return s_b; }
const char *fxtk_win_backend_name(void) { return s_b && s_b->name ? s_b->name : "none"; }

int  fxtk_win_init(void) { return (s_b && s_b->init) ? s_b->init() : -1; }
int  fxtk_win_count(void) { return (s_b && s_b->count) ? s_b->count() : 0; }
int  fxtk_win_create(int w,int h,int x,int y,const char *title)
{
    return (s_b && s_b->create) ? s_b->create(w,h,x,y,title) : -1;
}
int  fxtk_win_create_ex(int w,int h,int x,int y,const char *title,unsigned flags)
{
    if (s_b && s_b->create_ex) return s_b->create_ex(w,h,x,y,title,flags);
    return (s_b && s_b->create) ? s_b->create(w,h,x,y,title) : -1;
}
void fxtk_win_destroy(int id) { if (s_b && s_b->destroy) s_b->destroy(id); }
void fxtk_win_show(int id,int on) { if (s_b && s_b->show) s_b->show(id,on); }
void fxtk_win_raise(int id) { if (s_b && s_b->raise_win) s_b->raise_win(id); }
void fxtk_win_move(int id,int x,int y) { if (s_b && s_b->move) s_b->move(id,x,y); }
void fxtk_win_resize(int id,int w,int h) { if (s_b && s_b->resize) s_b->resize(id,w,h); }
void fxtk_win_title(int id,const char *t) { if (s_b && s_b->set_title) s_b->set_title(id,t); }
void fxtk_win_begin(int id) { if (s_b && s_b->begin) s_b->begin(id); }
void fxtk_win_end(int id) { if (s_b && s_b->end) s_b->end(id); }
int  fxtk_win_width(int id) { return (s_b && s_b->width) ? s_b->width(id) : 0; }
int  fxtk_win_height(int id) { return (s_b && s_b->height) ? s_b->height(id) : 0; }
void fxtk_win_poll(void) { if (s_b && s_b->poll) s_b->poll(); }
void fxtk_win_client_offset(int id, int *ox, int *oy)
{
    if (ox) *ox = 0; if (oy) *oy = 0;
    if (s_b && s_b->client_offset) s_b->client_offset(id, ox, oy);
}
void fxtk_win_screen(int *w, int *h)
{
    if (w) *w = 0; if (h) *h = 0;
    if (s_b && s_b->screen_size) s_b->screen_size(w, h);
}
int  fxtk_win_quit(void) { return (s_b && s_b->quit_requested) ? s_b->quit_requested() : 0; }
double fxtk_win_time(void) { return (s_b && s_b->time) ? s_b->time() : 0.0; }
