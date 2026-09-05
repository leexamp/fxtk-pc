/* canvas_08_files.c — 文件夹浏览器组件 (v2.3)
 * 系统对话框选文件夹(Win32 SHBrowseForFolder / Linux zenity) + 列目录成表格(名称/日期/大小)。
 * 跨平台: 同一份源码在 Linux 与 Windows 各走 POSIX / Win32 分支。
 */
#include "fxtk.h"
#include "fxtk_fs.h"
#include "fxtk_desktop.h"   /* fx_wheel_take */
#include <string.h>

/* ---- 浏览器状态 ---- */
static char      s_dir[512] = ".";          /* 当前目录, 默认当前工作目录 */
static fx_fs_entry_t s_ent[128];
static int       s_n = 0;
static int       s_scroll = 0;              /* 表格滚动 */

/* "选择文件夹预览": 系统对话框选文件夹, 然后列表 */
static void on_pick(fx_widget_t *w, void *ud) {
    (void)w; (void)ud;
    char d[512];
    if (fx_fs_pick_dir(d, sizeof(d))) {
        strncpy(s_dir, d, sizeof(s_dir) - 1); s_dir[sizeof(s_dir)-1] = 0;
        s_n = fx_fs_list(s_dir, s_ent, 128);
        s_scroll = 0;
        fx_set_title(fx_find("dir_lbl"), s_dir);
        fx_repaint();
    }
}
/* 画文件夹内容表格 */
static void on_view(fx_widget_t *w, void *ud) {
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(250, 250, 250));
    /* 表头 */
    fx_set_color(FX_BTN_BLUE); fx_fill_rect(0, 0, cw - 1, 18);
    fx_set_color(FX_WHITE);
    fx_draw_text_c(8,  3, "Name", FX_WHITE, FX_BTN_BLUE);
    fx_draw_text_c(240,3, "Size", FX_WHITE, FX_BTN_BLUE);
    fx_draw_text_c(310,3, "Modified", FX_WHITE, FX_BTN_BLUE);
    /* 行 (过滤隐藏 . 开头, 目录蓝字) */
    int rh = 20, y0 = 24, shown = 0;
    for (int i = 0; i < s_n; i++) {
        if (s_ent[i].name[0] == '.') continue;
        int y = y0 + shown * rh - s_scroll;
        if (y + rh < 0 || y > ch) { shown++; continue; }
        fx_set_color(FX_LGRAY); fx_fill_rect(2, y, cw - 2, y + rh - 2);
        fx_color_t c = s_ent[i].is_dir ? FX_BTN_BLUE : FX_UI_FG;
        fx_draw_text_c(6, y + 2, s_ent[i].name, c, FX_RGB(250, 250, 250));
        char sz[32]; snprintf(sz, sizeof(sz), "%ld", s_ent[i].size);
        fx_draw_text_c(240, y + 2, sz,    c, FX_RGB(250, 250, 250));
        fx_draw_text_c(310, y + 2, s_ent[i].date, c, FX_RGB(250, 250, 250));
        shown++;
    }
    fx_set_color(FX_LGRAY); fx_draw_hline(0, cw - 1, 0);
}
/* 滚轮滚动表格 */
static void on_wheel(fx_widget_t *w, void *ud) {
    (void)w; (void)ud;
    int d = fx_wheel_take(w);
    s_scroll -= d;
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > 1200) s_scroll = 1200;
    fx_repaint();
}
void app_init(void) {
    fx_set_bg(FX_WINDOW_BG);
    /* 选择文件夹按钮 + 当前路径标签 */
    fx_button_new(pixel("8,8", "108,34"), title("选择文件夹预览"), color(FX_BTN_BLUE), call(on_pick));
    fx_label_new(pixel("116,12", "440,30"), name("dir_lbl"), title(s_dir), fgcolor(FX_RGB(40,40,40)));
    /* 文件夹内容表格 (可滚动) */
    fx_canvas_new(pixel("8,40", "440,230"), name("fs"), anim(1), color(FX_RGB(250,250,250)), call(on_view));
    fx_set_cb(fx_find("fs"), on_wheel, NULL);
    /* 初始列当前目录 */
    s_n = fx_fs_list(s_dir, s_ent, 128);
}
