/* ============================================================================
 * your_app/main.c —— 最小可运行示例（从这里开始改你自己的应用）
 *
 * 只有三件事：① 初始化驱动与框架  ② 用属性宏建控件  ③ 每帧调 fx_poll + 提交渲染
 * 编译运行：  cd your_app && make && ./your_app
 * ==========================================================================*/
#include "sokol_app.h"
#include "fxtk.h"
#include <stdio.h>

extern fx_driver_t fx_sokol_driver;              /* drivers/fxtk_sokol_driver.c */
void fxtk_sokol_frame(int fb_w, int fb_h);       /* 提交本帧（画到窗口） */
extern void fxtk_font_init(const char *font_path, int size);   /* drivers/fxtk_font_stb.c */

/* ⚠️ 必读难点：**文字渲染必须先初始化字体层**。忘了这一步，界面上的按钮/滑条都在，
 *    但一个字都不会显示（本项目最初就是这么被绊住的）。
 *    传 NULL 表示"用系统字体"（找不到会自动回退），size 是默认字号。 */


static fx_widget_t *s_label = 0;
static int s_count = 0;

/* 控件回调：点一下就更新标签文字 */
static void on_click(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    char buf[64];
    snprintf(buf, sizeof(buf), "按钮被点了 %d 次", ++s_count);
    if (s_label) fx_set_title(s_label, buf);     /* 改文字后框架会自己重绘该区域 */
}

static void init_cb(void)                        /* ① 初始化 */
{
    fx_sokol_driver.width  = sapp_width();
    fx_sokol_driver.height = sapp_height();
    fx_sokol_driver.init();
    fxtk_font_init(NULL, 18);                    /* ← 没有这行就没有文字！ */
    fx_init(&fx_sokol_driver);

    /* ② 建界面：pixel() 使用 480x272 设计坐标，窗口缩放时自动等比放大 */
    fx_label_new(pixel("16,20", "300,40"), title("你好，fxtk"), fgcolor(FX_RGB(40, 40, 40)));
    s_label = fx_label_new(pixel("16,48", "300,68"), title("按钮被点了 0 次"),
                           fgcolor(FX_RGB(120, 120, 120)));
    fx_button_new(pixel("16,88", "180,124"), title("点我"), name("hello"),
                  color(FX_RGB(255, 90, 0)), call(on_click));
    fx_slider_new(pixel("200,96", "440,112"), value(60), color(FX_RGB(33, 150, 243)));
}

static void frame_cb(void)                       /* ③ 每帧 */
{
    fx_poll();                                   /* 处理输入、触发重绘 */
    fxtk_sokol_frame(sapp_width(), sapp_height());
}

sapp_desc sokol_main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .width = 640, .height = 360,
        .window_title = "your_app — fxtk 起步示例",
    };
}
