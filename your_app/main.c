/* ============================================================================
 * your_app/main.c —— 最小应用（从这里开始写你自己的）
 *
 * 只有两个头，**不出现 sokol**；窗口、字体、屏幕尺寸、每帧调度都由 fxtk_app 外壳负责。
 * 编译运行：cd your_app && ./build.sh
 * ==========================================================================*/
#include "fxtk.h"
#include "fxtk_app.h"
#include <stdio.h>

static fx_widget_t *s_label = 0;
static int s_count = 0;

static void on_click(fx_widget_t *w, void *ud)          /* 控件回调：签名固定 */
{
    (void)w; (void)ud;
    char buf[64];
    snprintf(buf, sizeof(buf), "按钮被点了 %d 次", ++s_count);
    if (s_label) fx_set_title(s_label, buf);            /* 改文字，框架自己重绘该区域 */
}

void fxtk_app_init(void)                                 /* ① 建界面 */
{
    /* pixel() 用 480x272 设计坐标，窗口缩放时自动等比放大 */
    fx_label_new(pixel("16,20", "300,40"), title("你好，fxtk"), fgcolor(FX_RGB(40, 40, 40)));
    s_label = fx_label_new(pixel("16,48", "300,68"), title("按钮被点了 0 次"),
                           fgcolor(FX_RGB(120, 120, 120)));
    fx_button_new(pixel("16,88", "180,124"), title("点我"), name("hello"),
                  color(FX_RGB(255, 90, 0)), call(on_click));
    fx_slider_new(pixel("200,96", "440,112"), value(60), color(FX_RGB(33, 150, 243)));
}

/* 选写：每帧回调（动画/定时逻辑放这里）
 * void fxtk_app_frame(void) { }
 * 选写：窗口标题与尺寸
 * const char *fxtk_app_title(void) { return "我的应用"; }
 * void fxtk_app_size(int *w, int *h) { *w = 960; *h = 540; }
 */
