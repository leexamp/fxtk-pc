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

/* ==================== v2.5: 单窗口桌面行为 ====================
 * 【为什么这些能做, 而"多窗口"不能】
 *   sokol_app 是单窗口设计(全局状态 + 一次 sapp_run) —— 多窗口必须换平台层。
 *   但它对外暴露了【原生窗口句柄】:
 *       sapp_win32_get_hwnd() / sapp_x11_get_window()+sapp_x11_get_display() / sapp_macos_get_window()
 *   所以"去标题栏 / 挪窗口 / 置顶 / 居中"这些【单窗口】行为不必换后端。
 *
 * 【支持矩阵】(不支持 = 安全空操作并返回 0, 绝不崩)
 *       Win32 : 去标题栏 ✅   置顶 ✅   移动 ✅   居中 ✅   拖动 ✅
 *       X11   : 去标题栏 ⚠️WM  置顶 ⚠️WM  移动 ✅   居中 ✅   拖动 ⚠️WM
 *       macOS : ❌ 需要 Objective-C 运行时, 待做 —— fx_win_available() 返回 0
 *       ESP32 / SDL 遗留后端 : ❌ 无此模块
 *
 * 【⚠️ 诚实声明, 请勿当成"已验证"】
 *   标 WM 的几项依赖【窗口管理器】配合(_MOTIF_WM_HINTS / _NET_WM_STATE / _NET_WM_MOVERESIZE)。
 *   在没有 WM 的环境(裸 X / Xvfb / 部分远程会话)里它们【不生效】, 而且【无法在程序里探测出来】——
 *   因为 X 不会回报"这条 EWMH 请求被忽略了"。所以返回值 1 只代表"请求已发出",
 *   不代表"屏幕上真的变了"。要确认视觉效果, 只能在真实桌面上目视核对。
 */
int  fx_win_available(void);          /* 当前环境是否可用 (0 = 所有调用将被忽略) */
int  fx_win_set_borderless(int on);   /* 去标题栏(1) / 恢复(0) */
int  fx_win_get_borderless(void);     /* 我们【请求过】的状态, 非屏幕实况 */
int  fx_win_set_always_on_top(int on);
int  fx_win_get_always_on_top(void);  /* 同上: 请求状态 */
/* 【回查屏幕实况, 不是请求记录】读 WM 的 _NET_FRAME_EXTENTS(Win32 读 WS_CAPTION):
 *     1 = 有装饰   0 = 无装饰(=去标题栏真的生效了)   -1 = 该环境问不出来
 * 这个函数存在的意义: 去标题栏在各 WM 上支持度差异极大(实测 GNOME/mutter 对
 * _MOTIF_WM_HINTS 完全不理, 连 remap 都不行), 光看 set 的返回值会误判成"已生效"。
 * 要断言"生效", 必须回查本函数 —— 这正是项目里"别把没验证的写成已修"的落实。 */
int  fx_win_get_decorated(void);
int  fx_win_move(int x, int y);       /* 移到屏幕坐标 (屏幕左上原点) */
int  fx_win_get_pos(int *x, int *y);  /* 取窗口屏幕坐标; x/y 可为 NULL */
int  fx_win_center(void);
void fx_win_drag_begin(void);         /* 无边框窗口: 鼠标按下时调用 → 交给系统拖动 */

#endif /* FXTK_DESKTOP_H */
