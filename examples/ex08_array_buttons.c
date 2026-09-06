/** ex08_array_buttons — 按钮"数组化": 配置驱动循环创建 + 句柄数组批量管理 */
#include "fxtk.h"
#include <stdio.h>

#define N 6
static const char *s_names[N] = { "通道1","通道2","通道3","通道4","通道5","通道6" };
static fx_widget_t *s_btns[N];   /* ★ 句柄数组: 创建时存指针, 之后批量操作 */
static fx_widget_t *s_lab;
static int s_sel = -1;

/* 批量重绘: 选中=蓝, 其余=灰 (循环句柄数组 = 批量管理) */
static void paint(void)
{
    for (int i = 0; i < N; i++)
        fx_set_color_w(s_btns[i], i == s_sel ? FX_RGB(33,150,243) : FX_RGB(158,158,158));
}
/* 共享回调: 靠 ud 区分"我是第几个" (不用写 6 个回调) */
static void on_sel(fx_widget_t *w, void *ud)
{
    s_sel = (int)(intptr_t)ud;
    paint();
    char b[32]; snprintf(b, sizeof(b), "已选: %s", s_names[s_sel]);
    fx_set_title(s_lab, b);
}
static void on_clear(fx_widget_t *w, void *ud)
{
    s_sel = -1;
    paint();
    fx_set_title(s_lab, "已选: 无");
}

void app_init(void)
{
    fx_set_bg(FX_RGB(240, 240, 240));
    fx_label_new(percent("0.03,0.04", "0.97,0.14"),
                 title("fxtk 示例 08 — 数组化创建按钮"), fgcolor(FX_RGB(40, 40, 40)));

    /* ★ 核心: 配置/坐标全由循环算出, 2 行 × 3 列, 改 N 就改规模 */
    for (int i = 0; i < N; i++) {
        int r = i / 3, c = i % 3;
        char a[16], b[16];   /* pixel() 收运行时字符串也OK, 解析发生在 new 内部 */
        snprintf(a, sizeof(a), "%d,%d", 20 + c * 150, 60 + r * 55);
        snprintf(b, sizeof(b), "%d,%d", 20 + c * 150 + 140, 60 + r * 55 + 40);
        s_btns[i] = fx_button_new(pixel(a, b), title(s_names[i]),
                                  color(FX_RGB(158, 158, 158)), call(on_sel));
        fx_set_cb(s_btns[i], on_sel, (void *)(intptr_t)i);   /* ud = 下标 */
    }

    s_lab = fx_label_new(pixel("20,180", "300,205"), title("已选: 无"),
                         fgcolor(FX_RGB(60, 60, 60)));
    fx_button_new(pixel("330,180", "460,215"), title("全不选"),
                  color(FX_RGB(244, 67, 54)), call(on_clear));
}
