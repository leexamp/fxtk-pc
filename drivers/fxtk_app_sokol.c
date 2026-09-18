/* ============================================================================
 * fxtk_app_sokol.c —— sokol 应用外壳（驱动侧；应用代码不该引 sokol）
 *
 * 负责：sokol 入口与每帧调度、字体初始化、屏幕尺寸上报、驱动初始化。
 * 这些都是"漏了就会出怪问题"的步骤（漏字体=没文字；漏 set_screen=鼠标点不动），
 * 放在外壳里，应用只需要实现 fxtk_app_init()。
 * ==========================================================================*/
#include "sokol_app.h"
#include "fxtk.h"
#include "fxtk_app.h"
#include "fxtk_backends.h"   /* fx_backend_set_screen 的声明: 缺了会隐式声明, float 参数会被当 double 传 */

extern fx_driver_t fx_sokol_driver;                        /* fxtk_sokol_driver.c */
extern void fxtk_sokol_frame(int fb_w, int fb_h);
extern void fxtk_font_init(const char *font_path, int size);

/* 可选接口：应用没实现时用默认值（弱符号，GCC/Clang 支持） */
__attribute__((weak)) void fxtk_app_frame(void) {}
__attribute__((weak)) const char *fxtk_app_title(void) { return "fxtk app"; }
__attribute__((weak)) void fxtk_app_size(int *w, int *h) { *w = 640; *h = 360; }

static void app_init_cb(void)
{
    fx_sokol_driver.width  = sapp_width();
    fx_sokol_driver.height = sapp_height();
    fx_sokol_driver.init();
    fxtk_font_init(NULL, 18);                              /* 文字层：不做就没有字 */
    fx_init(&fx_sokol_driver);
    fx_backend_set_screen(sapp_width(), sapp_height(),     /* 不上报屏幕尺寸：鼠标点不动 */
                          (float)sapp_dpi_scale());
    fxtk_app_init();                                       /* ← 你的界面 */
}

/* 事件入口：sokol 的鼠标/键盘事件必须转交给驱动，否则界面完全收不到输入
 * （外壳最初漏了这个 event_cb，表现就是"鼠标点不动"。演示的 main 里一直有这一步。） */
static void app_event_cb(const sapp_event *e)
{
    extern void fxtk_sokol_handle_event(const sapp_event *e);
    fxtk_sokol_handle_event(e);
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_ESCAPE)
        sapp_request_quit();
}

static void app_frame_cb(void)
{
    fx_poll();
    fxtk_app_frame();
    fxtk_sokol_frame(sapp_width(), sapp_height());
}

sapp_desc sokol_main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int w = 0, h = 0;
    fxtk_app_size(&w, &h);
    return (sapp_desc){
        .init_cb = app_init_cb,
        .frame_cb = app_frame_cb,
        .event_cb = app_event_cb,
        .width = w > 0 ? w : 640,
        .height = h > 0 ? h : 360,
        .window_title = fxtk_app_title(),
        /* ↓ 这三行缺了会出怪问题: 2D UI 的管线不带深度, 交换链也必须不带,
         *   否则【拖动窗口时交换链重建 → 格式冲突 → 黑屏】(稳定后才恢复)。
         *   演示里一直有这几行, 外壳漏掉就复现了同样的现象。 */
        .depth_format = SAPP_PIXELFORMAT_NONE,
        .swap_interval = 1,
        .high_dpi = false,
    };
}
