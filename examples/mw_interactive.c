/* examples/mw_interactive.c — 多真实窗口 + 控件层输入闭环 demo (v2.5.2)
 *
 * 目的: 证明 FXTK_MW 不只"能画多窗口", 还能把每扇窗的鼠标/键盘事件投递给控件层:
 *   - 每扇窗一棵独立控件树 (fx_root_new / fx_root_select);
 *   - 每帧在该窗口 begin/end 之间调 fx_poll(), 输入只作用于这棵树;
 *   - 按钮点击/滑杆拖动/复选框/文本框/右键菜单/焦点切换都是控件层真实行为。
 *
 * 构建+运行:
 *   make -C demo-main mw_interactive && ./demo-main/mw_interactive
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include "fxtk_win.h"
#include "fxtk_mw.h"
#include "fxtk_backends.h"   /* fx_sleep_ms */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fx_widget_t *roots[2];
static int win_ids[2];
static fx_widget_t *lbl_clicks, *lbl_slider, *lbl_events;
static int clicks = 0, events = 0;

static void bump_events(void) { events++; if (lbl_events) { char b[96]; snprintf(b, sizeof b, "events: %d", events); fx_set_title(lbl_events, b); } }

static void on_click(fx_widget_t *w, void *ud) { (void)w; (void)ud; clicks++; char b[64];
    snprintf(b, sizeof b, "clicks: %d", clicks); fx_set_title(lbl_clicks, b); bump_events();
    printf("[mw-demo] button clicked, clicks=%d\n", clicks); fflush(stdout); fx_repaint(); }
static void on_check(fx_widget_t *w, void *ud) { (void)ud; bump_events();
    printf("[mw-demo] checkbox -> %d (%s)\n", fx_get_value(w), fx_widget_title(w) ? fx_widget_title(w) : ""); fflush(stdout); }
static void on_slider(fx_widget_t *w, void *ud) { (void)ud; char b[64];
    snprintf(b, sizeof b, "slider: %d", fx_get_value(w)); fx_set_title(lbl_slider, b); bump_events(); fx_repaint(); }
static void on_raise(fx_widget_t *w, void *ud) { (void)w; (void)ud; fxtk_win_raise(win_ids[0]); bump_events();
    printf("[mw-demo] raise controls window\n"); fflush(stdout); }
static void on_move(fx_widget_t *w, void *ud) { (void)w; (void)ud; static int dx = 0; dx += 20;
    fxtk_win_move(win_ids[0], 60 + dx, 60); bump_events();
    printf("[mw-demo] move controls +20 -> x=%d\n", 60 + dx); fflush(stdout); }
static void on_resize(fx_widget_t *w, void *ud) { (void)w; (void)ud; static int dw = 0; dw += 40;
    fxtk_win_resize(win_ids[0], 640 + dw, 360); bump_events();
    printf("[mw-demo] resize controls +40 -> %d\n", 640 + dw); fflush(stdout); }
static void on_quit(fx_widget_t *w, void *ud) { (void)w; (void)ud; printf("[mw-demo] quit button\n"); fflush(stdout); exit(0); }

static void build_controls(fx_widget_t *r)
{
    fx_root_select(r);
    fx_label_new(pixel("16,12","500,40"), title("fxtk 多窗口交互 demo"), fgcolor(FX_RGB(33,150,243)));
    fx_button_new(pixel("16,50","200,86"), name("btn"), title("点击我"), call(on_click));
    lbl_clicks = fx_label_new(pixel("220,56","500,82"), name("clicks"), title("clicks: 0"));
    fx_textedit_new(pixel("16,100","500,136"), name("te"), title("在这里打字 / 右键有菜单"));
    fx_checkbox_new(pixel("16,150","320,180"), name("cb"), title("勾选我"), call(on_check));
    fx_slider_new(pixel("16,190","500,210"), name("sl"), value(40), call(on_slider));
    lbl_slider = fx_label_new(pixel("16,218","500,244"), name("slv"), title("slider: 40"));
    lbl_events = fx_label_new(pixel("16,250","500,276"), name("ev"), title("events: 0"));
    fx_button_new(pixel("16,286","200,322"), name("quit"), title("关闭此窗"), call(on_quit));
}

static void build_panel(fx_widget_t *r)
{
    fx_root_select(r);
    fx_label_new(pixel("16,12","380,40"), title("窗口控制"), fgcolor(FX_RGB(126,231,135)));
    fx_button_new(pixel("16,50","464,86"), title("Raise controls"), call(on_raise));
    fx_button_new(pixel("16,94","464,130"), title("Move controls +20"), call(on_move));
    fx_button_new(pixel("16,138","464,174"), title("Resize controls +40"), call(on_resize));
    fx_label_new(pixel("16,186","464,214"), title("点击左边窗口的控件试试"));
    fx_label_new(pixel("16,214","464,242"), title("Esc / 关任意窗口 = 退出"));
}

int main(void)
{
    fxtk_mw_register();
    fxtk_win_init();
    win_ids[0] = fxtk_win_create(640, 360, 60, 60, "fxtk controls");
    win_ids[1] = fxtk_win_create(480, 272, 740, 120, "fxtk panel");
    if (win_ids[0] < 0 || win_ids[1] < 0) { fprintf(stderr, "[mw-demo] create window failed\n"); return 1; }
    roots[0] = fx_root_new();
    roots[1] = fx_root_new();
    if (!roots[0] || !roots[1]) { fprintf(stderr, "[mw-demo] fx_root_new failed\n"); return 1; }
    build_controls(roots[0]);
    build_panel(roots[1]);
    fxtk_win_show(win_ids[0], 1);
    fxtk_win_show(win_ids[1], 1);
    printf("[mw-demo] windows ready: controls=%d panel=%d\n", win_ids[0], win_ids[1]);
    fflush(stdout);

    while (!fxtk_win_quit()) {
        fxtk_win_poll();
        for (int i = 0; i < 2; i++) {
            fxtk_win_begin(win_ids[i]);
            fx_root_select(roots[i]);
            fx_repaint();        /* demo: 每帧整窗重绘, 简单直观 */
            fx_poll();           /* 处理该窗口暂存的输入 + 绘制该根 */
            fxtk_win_end(win_ids[i]);
        }
        fx_sleep_ms(8);
    }
    return 0;
}
