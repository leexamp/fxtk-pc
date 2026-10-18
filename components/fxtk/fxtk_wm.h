/**
 * fxtk_wm.h — v3 合成式多窗口管理器 (画布内合成, 分辨率无关)
 *
 * 与 v2 的"每个窗口是真控件"不同, v3 把窗口内容先渲染到离屏画布, 再作为图片
 * 合成到一块全屏画布上 —— 于是可以缩放/旋转/任意位置叠放, 圆角/边框/高光都
 * 直接画在合成画布上(背景已铺好, 圆角外自然就是背景), 不需要 alpha 通道。
 *
 * 用法:
 *   fx_wm_set_ui(scale);                       // 分辨率缩放
 *   int a = fx_wm_add("fxtk", FX_WM_LINUX, 640, 360);
 *   fx_wm_set(a, x, y, w, h);
 *   fx_wm_prepare(draw_content, ud);           // 画布回调开头: 先离屏渲染所有内容
 *   ...画背景...
 *   fx_wm_draw();                              // 再按 z 序合成窗口
 *   ...画歌词/叠加层...
 */
#ifndef FXTK_WM_H
#define FXTK_WM_H
#include "fxtk.h"

#define FX_WM_LINUX 0
#define FX_WM_WIN11 1
#define FX_WM_TERM  2
#define FX_WM_PLAIN 3

typedef void (*fx_wm_content_fn)(int idx, int cw, int ch, void *ud);

int   fx_wm_add(const char *title, int style, int cw, int ch);
void  fx_wm_set(int idx, float x, float y, float w, float h);
void  fx_wm_set_style(int idx, int st);
void  fx_wm_set_title(int idx, const char *title);
void  fx_wm_set_z(int idx, int z);
void  fx_wm_set_focus(int idx, int focus);
void  fx_wm_set_visible(int idx, int vis);
void  fx_wm_set_rot(int idx, float deg);
void  fx_wm_set_ui(float k);
void  fx_wm_prepare(fx_wm_content_fn content, void *ud);   /* 兼容保留(空操作) */
void  fx_wm_draw(fx_wm_content_fn content, void *ud);      /* 画所有窗口: 边框/标题栏/内容 */
int   fx_wm_count(void);
void  fx_wm_get(int idx, float *x, float *y, float *w, float *h, int *style, int *focus);
int   fx_wm_title_h(int style);
#endif
