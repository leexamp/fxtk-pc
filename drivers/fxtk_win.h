/* ============================================================================
 * fxtk_win.h —— 多真实窗口能力 (后端无关的应用接口)
 *
 * 应用只依赖这里, 不包含/不调用任何 sokol、X11、Win32 API。
 * 后端 (sokol 单窗口 / GL 多窗口 / 软件多窗口 / 未来的 Vulkan ...) 通过
 * fxtk_win_backend_set() 注册一个 vtable:
 *
 *   应用:   fxtk_win_create() -> 每帧  begin 画UI end
 *   后端:   创建原生窗口、GL 上下文、交换链、blit ...
 *
 * 没有注册后端时, 所有接口是安全空操作(返回 -1 / 0), 单窗口后端无需实现。
 * ==========================================================================*/
#ifndef FXTK_WIN_H
#define FXTK_WIN_H
#include "fxtk.h"

typedef struct fxtk_win_backend {
    const char *name;
    int  (*init)(void);
    void (*shutdown)(void);
    int  (*count)(void);
    int  (*create)(int w, int h, int x, int y, const char *title); /* 返回窗口 id */
    void (*destroy)(int id);
    void (*show)(int id, int on);
    void (*move)(int id, int x, int y);
    void (*resize)(int id, int w, int h);
    void (*set_title)(int id, const char *t);
    void (*begin)(int id);   /* 设当前渲染目标(实现内部 make current + 上报尺寸) */
    void (*end)(int id);     /* 渲染本窗口并 present */
    int  (*width)(int id);
    int  (*height)(int id);
    void (*poll)(void);
    int  (*quit_requested)(void);
    double (*time)(void);    /* 单调秒 */
    void (*raise_win)(int id);   /* 置顶 (可选) */
    /* 可选: 带标志的创建 (无边框等); 为空则回退 create */
    int  (*create_ex)(int w, int h, int x, int y, const char *title, unsigned flags);
    /* 可选: 屏幕尺寸 (应用用来把 1280x720 舞台居中摆放); 为空返回 0 */
    void (*screen_size)(int *w, int *h);
    /* 可选: 客户区相对窗口外框的偏移 (标题栏/边框); 应用用来把客户区摆到指定位置 */
    void (*client_offset)(int id, int *ox, int *oy);
} fxtk_win_backend;

/* 后端注册 (在 fxtk_win_init 之前调用) */
void        fxtk_win_backend_set(const fxtk_win_backend *b);
const char *fxtk_win_backend_name(void);
const fxtk_win_backend *fxtk_win_backend_get(void);

/* ---------- 应用侧 API ---------- */
int  fxtk_win_init(void);                     /* 初始化当前后端, 0=成功 */
int  fxtk_win_count(void);
#define FXTK_WIN_BORDERLESS 1u
int  fxtk_win_create(int w, int h, int x, int y, const char *title);
int  fxtk_win_create_ex(int w, int h, int x, int y, const char *title, unsigned flags);
void fxtk_win_destroy(int id);
void fxtk_win_show(int id, int on);
void fxtk_win_raise(int id);
void fxtk_win_move(int id, int x, int y);
void fxtk_win_resize(int id, int w, int h);
void fxtk_win_title(int id, const char *t);
void fxtk_win_begin(int id);
void fxtk_win_end(int id);
int  fxtk_win_width(int id);
int  fxtk_win_height(int id);
void fxtk_win_poll(void);
void fxtk_win_screen(int *w, int *h);   /* 屏幕像素尺寸 (后端提供; 无后端则返回 0) */
void fxtk_win_client_offset(int id, int *ox, int *oy);  /* 客户区相对外框偏移 */
int  fxtk_win_quit(void);
double fxtk_win_time(void);
#endif
