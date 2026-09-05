/**
 * fxtk_internal.h — fxtk 内部共享定义
 */
#ifndef FXTK_INTERNAL_H
#define FXTK_INTERNAL_H

#include "fxtk_image.h"

#define FX_MAX_WIDGETS 4096
#define FX_TAB_H 24   /* 顶部/底部标签条高度 */
#define FX_TAB_SIDE 88  /* 左/右侧边栏标签条宽度 (英文标签较长时用) */

#define FX_F_VISIBLE  0x01
#define FX_F_PRESSED  0x02
#define FX_F_ANIM     0x04
#define FX_F_BUF      0x08

#define FX_POS_PIXEL   0
#define FX_POS_PERCENT 1
#define FX_POS_GRID    2

/* ============ v2.3 可选控件编译 (减小体积) ============
 * 用 -DFXTK_WIDGET_XXX=0 编译时裁掉对应控件的实现(绘制/创建), 减小二进制体积。
 * 默认全开(1), PC demo 全开行为不变; 主要给 ESP32 等体积受限平台按需裁剪。
 * 关闭的控件仍可创建(枚举不变), 但不会被绘制; 请只在实际用到的平台裁剪。 */
#ifndef FXTK_WIDGET_BUTTON
#define FXTK_WIDGET_BUTTON 1
#endif
#ifndef FXTK_WIDGET_LABEL
#define FXTK_WIDGET_LABEL 1
#endif
#ifndef FXTK_WIDGET_GRID
#define FXTK_WIDGET_GRID 1
#endif
#ifndef FXTK_WIDGET_CANVAS
#define FXTK_WIDGET_CANVAS 1
#endif
#ifndef FXTK_WIDGET_SLIDER
#define FXTK_WIDGET_SLIDER 1
#endif
#ifndef FXTK_WIDGET_PROGRESS
#define FXTK_WIDGET_PROGRESS 1
#endif
#ifndef FXTK_WIDGET_CHECKBOX
#define FXTK_WIDGET_CHECKBOX 1
#endif
#ifndef FXTK_WIDGET_PANEL
#define FXTK_WIDGET_PANEL 1
#endif
#ifndef FXTK_WIDGET_TAB
#define FXTK_WIDGET_TAB 1
#endif
#ifndef FXTK_WIDGET_IMAGE
#define FXTK_WIDGET_IMAGE 1
#endif
#ifndef FXTK_WIDGET_TEXTEDIT
#define FXTK_WIDGET_TEXTEDIT 1
#endif
#ifndef FXTK_WIDGET_LIST
#define FXTK_WIDGET_LIST 1
#endif
#ifndef FXTK_WIDGET_DROP
#define FXTK_WIDGET_DROP 1
#endif

struct fx_widget {
    uint8_t type;
    uint16_t flags;
    int16_t x1, y1, x2, y2;
    uint8_t pos_mode;
    int16_t ox1, oy1, ox2, oy2;
    int16_t px1, py1, px2, py2;
    int16_t gr1, gc1, gr2, gc2;
    fx_widget_t *grid_ref;
    char name[24];
    char title[128];
    char *text_buf; int text_cap;
    int caret, anchor, text_max;
    int16_t scroll_y, content_h;  /* 滚动偏移 / 内容总高 */  /* 光标/选区锚点/字数上限(0=无限) */  /* TEXTEDIT 动态文本 (可无限长) */
    fx_cb_t cb;
    void *ud;
    fx_color_t bg, fg;
    uint8_t border, radius;
    uint8_t tab_side;   /* 标签页侧边栏方位: FX_TAB_TOP/LEFT/RIGHT/BOTTOM */
    int16_t value;
    int16_t page;
    int16_t lines, rows;
    fx_image_t *img;            /* 图片资源 (FX_W_IMAGE) */
    fx_widget_t *parent, *child, *sibling;
    
    /* 离屏缓冲相关 (Canvas) - 修复内存泄漏核心 */
    uint32_t *offbuf;
    uint16_t offw, offh;
};

extern fx_widget_t s_root;

void fxtk_draw_all(void);
fx_widget_t *fxtk_alloc(void);
void fxtk_free(fx_widget_t *w);
void fxtk_link(fx_widget_t *parent, fx_widget_t *child);
void fxtk_draw_canvases(void);
void fxtk_off_begin(fx_widget_t *cv);
void fxtk_off_end(fx_widget_t *cv);

void fxtk_draw_set_driver(const fx_driver_t *drv);
void fxtk_draw_flush_all(void);

#if FXTK_WIDGET_BUTTON
void fxtk_draw_button(fx_widget_t *w);
#endif
#if FXTK_WIDGET_LABEL
void fxtk_draw_label(fx_widget_t *w);
#endif
#if FXTK_WIDGET_GRID
void fxtk_draw_grid(fx_widget_t *w);
#endif
#if FXTK_WIDGET_CANVAS
void fxtk_draw_canvas(fx_widget_t *w);
#endif
#if FXTK_WIDGET_SLIDER
void fxtk_draw_slider(fx_widget_t *w);
#endif
#if FXTK_WIDGET_PROGRESS
void fxtk_draw_progress(fx_widget_t *w);
#endif
#if FXTK_WIDGET_CHECKBOX
void fxtk_draw_checkbox(fx_widget_t *w);
#endif
#if FXTK_WIDGET_PANEL
void fxtk_draw_panel(fx_widget_t *w);
#endif
#if FXTK_WIDGET_TAB
void fxtk_draw_tab(fx_widget_t *w);
#endif
#if FXTK_WIDGET_IMAGE
void fxtk_draw_image(fx_widget_t *w);
#endif

#endif
