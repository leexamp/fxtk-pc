/* ============================================================================
 * ref_app.c —— fxtk skill 的参考实现（ground truth）
 *
 * 这个文件的存在意义：它是 REFERENCE.md 里所有 API 的**可编译证据**。
 * 小模型不确定某个 API 怎么写时，从这里抄最近的块，而不是自己编。
 *
 * 编译验证：./verify.sh
 *
 * 覆盖：label / button / checkbox / slider / progress / textedit(含 readonly)
 *      / canvas 动画绘制 / fx_find / fx_set_title / fx_get_value / fx_canvas_size
 * ==========================================================================*/
#include "fxtk.h"          /* 核心控件 + 绘图 */
#include "fxtk_desktop.h"  /* fx_textedit_new, maxlen, fx_set_fgcolor */
#include "fxtk_app.h"      /* 外壳：它提供 main()，你不写 main */
#include <stdio.h>

static fx_widget_t *s_status = 0;   /* 需要后续更新的控件，存指针 */
static fx_widget_t *s_edit   = 0;
static int s_count = 0;

/* ---- 回调：签名固定 void fn(fx_widget_t *w, void *ud) ---- */
static void on_click(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    char buf[64];
    snprintf(buf, sizeof buf, "点击 %d 次", ++s_count);
    if (s_status) fx_set_title(s_status, buf);   /* 改文字会自动重绘 */
}

static void on_check(fx_widget_t *w, void *ud)
{
    (void)ud;
    int on = fx_get_value(w);                    /* 复选框取值 */
    /* fx_textedit_set_readonly 是 v2.4.5 才补上实现的 API。
     * 这里用 FXTK_HAVE_READONLY 守卫：在更旧的检出上退化为"只改提示文字"，
     * 使参考实现（以及 verify.sh）不会因为下游版本较旧而链接失败。
     * 你自己的程序若确定目标是 v2.4.5+，直接调用即可，不需要这个守卫。 */
#ifdef FXTK_HAVE_READONLY
    if (s_edit) fx_textedit_set_readonly(s_edit, on);
#endif
    if (s_status) fx_set_title(s_status, on ? "文本框已锁定" : "文本框可编辑");
}

static void on_slide(fx_widget_t *w, void *ud)
{
    (void)ud;
    char buf[64];
    snprintf(buf, sizeof buf, "滑条 = %d", fx_get_value(w));
    if (s_status) fx_set_title(s_status, buf);
}

/* ---- 画布回调：本地坐标，(0,0) 是画布左上角 ---- */
static void on_draw(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw = 0, ch = 0;
    fx_canvas_size(w, &cw, &ch);                 /* 取画布本地宽高 */

    fx_canvas_clear(w, FX_RGB(250, 250, 252));   /* 铺底 */

    fx_set_color(FX_RGB(33, 150, 243));
    fx_fill_circle(cw / 2, ch / 2, ch / 4);
    fx_fill_rect(8, 8, cw / 3, ch / 4);

    fx_set_color(FX_RGB(244, 67, 54));
    fx_draw_rect(1, 1, cw - 2, ch - 2);           /* 描边 */
    fx_draw_line(0, ch - 1, cw - 1, 0);

    fx_set_color(FX_RGB(255, 152, 0));
    fx_fill_rect_gradient(8, ch - 40, cw - 9, ch - 9, FX_YELLOW, FX_RED, 1);

    fx_set_color(FX_RGB(60, 60, 60));
    fx_draw_text(6, 6, "canvas");
}

/* ---- 必需入口：外壳调用它建界面（不要自己写 main） ---- */
void fxtk_app_init(void)
{
    fx_set_bg(FX_RGB(245, 245, 247));

    /* 左栏 300px 宽，右栏画布 */
    fx_label_new(pixel("12,10", "300,34"), name("ttl"), title("fxtk 参考程序"));
    s_status = fx_label_new(pixel("12,36", "300,58"), title("等待操作"));

    fx_button_new(pixel("12,66", "150,100"), name("go"), title("点我"), call(on_click));
    fx_checkbox_new(pixel("158,66", "300,100"), name("lock"), title("只读"), call(on_check));

    fx_slider_new(pixel("12,108", "300,124"), name("sld"), value(60), call(on_slide));
    fx_progress_new(pixel("12,132", "300,146"), name("prg"), value(40));

    s_edit = fx_textedit_new(pixel("12,154", "300,186"), name("edit"),
                             title("可编辑文本"), maxlen(20));

    /* 画布：anim(1) 让回调每帧都跑（动画）；静态画布去掉 anim */
    fx_canvas_new(pixel("308,10", "468,262"), name("cv"), anim(1), call(on_draw));
}

/* 选写：每帧回调，动画/定时逻辑放这里
 * void fxtk_app_frame(void) { }
 */

/* 选写：窗口标题与初始尺寸
 * const char *fxtk_app_title(void) { return "fxtk 参考程序"; }
 * void fxtk_app_size(int *w, int *h) { *w = 800; *h = 460; }
 */
