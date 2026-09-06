#ifndef FXTK_DESKTOP_H
#define FXTK_DESKTOP_H
#include "fxtk.h"
#define fx_textedit_new(...) fx_widget_new_impl(FX_W_TEXTEDIT, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
void fxtk_draw_textedit(fx_widget_t *w);
void fx_set_focus(fx_widget_t *w);
fx_widget_t *fx_get_focus(void);
int  fx_focus_blink(void);
fx_keyev_t fx_last_key(void);
void fx_touch_state(int *x, int *y, int *pressed);
fx_widget_t *fx_pressed(void);
void fx_widget_set_rect(fx_widget_t *w, int x1, int y1, int x2, int y2);
void fx_set_fgcolor(fx_widget_t *w, fx_color_t c);
#define fx_scroll_new(...) fx_widget_new_impl(FX_W_SCROLL, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
void fx_scroll_content(fx_widget_t *w, int content_h);
fx_attr_t maxlen(int n);                 /* 字数上限, 带 n/max 计数 */
int  fx_text_width_n(const char *s, int n);
void fx_draw_text_c_n(int x, int y, const char *s, int n, fx_color_t fg, fx_color_t bg);
int fx_wheel_take(fx_widget_t *w);   /* 画布滚轮增量, 取后清零 */

#endif /* FXTK_DESKTOP_H */
