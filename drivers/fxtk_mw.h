/* fxtk_mw.h —— 多窗口后端内部接口 (fxtk_sokol_mw.c 实现)
 * 应用不要直接包含本文件: 应用用 fxtk_win.h (后端无关)。
 * 这里是 fxtk_sokol 后端的一部分, 内部可用 sokol_gfx / GLX / WGL。
 */
#ifndef FXTK_MW_H
#define FXTK_MW_H
#include <stdint.h>

typedef struct fxtk_mw_desc { const char *title; int x, y, w, h; unsigned flags; } fxtk_mw_desc;
#define FXTK_MW_BORDERLESS 1u

int  fxtk_mw_init(void);                 /* 建 GL 上下文 + 完成 fxtk 驱动初始化 */
void fxtk_mw_shutdown(void);
int  fxtk_mw_count(void);
int  fxtk_mw_window_create(const fxtk_mw_desc *d);
void fxtk_mw_window_destroy(int id);
void fxtk_mw_window_show(int id, int on);
void fxtk_mw_window_move(int id, int x, int y);
void fxtk_mw_window_resize(int id, int w, int h);
void fxtk_mw_window_title(int id, const char *t);
void fxtk_mw_window_raise(int id);
void fxtk_mw_make_current(int id);
void fxtk_mw_present(int id);
void fxtk_mw_screen_size(int *w, int *h);   /* 显示器像素尺寸 */
void fxtk_mw_client_offset(int id, int *ox, int *oy);  /* 客户区相对外框偏移 */
int  fxtk_mw_width(int id);
int  fxtk_mw_height(int id);
void fxtk_mw_poll(void);
int  fxtk_mw_quit_requested(void);
double fxtk_mw_time(void);

int  fxtk_mw_sapp_width(void);
int  fxtk_mw_sapp_height(void);
void fxtk_mw_sapp_set_title(const char *s);
void fxtk_mw_sapp_ime_spot(int x, int y);

/* 注册为 fxtk_win 后端 (应用/外壳调用) */
void fxtk_mw_register(void);
#endif
