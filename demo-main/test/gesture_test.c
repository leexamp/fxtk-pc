/**
 * gesture_test.c — v2.5 触摸手势自检 (无头)
 *
 * 全部用【合成触摸序列】喂进去, 时基由本测试自己给 —— 所以:
 *   - 不需要触摸屏, 不需要窗口, 不需要真实时钟
 *   - 结果完全确定(不依赖机器快慢), 放进 CI 不会 flaky
 *
 * 这正是把它做成"喂样本→吐事件"纯状态机的原因: 手势逻辑若直接耦合驱动的
 * touch_read, 就只能靠真触摸屏测, 而真屏测手势几乎测不准。
 */
#include "fxtk.h"
#include <stdio.h>
#include <string.h>

static int s_fail = 0;

static void chk(const char *name, int ok)
{
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) s_fail++;
}

/* ---- 事件收集 ---- */
static fx_gesture_type_t s_ev[32];
static int s_evn = 0;

static void collect(void)
{
    fx_gesture_ev_t e;
    while (fx_gesture_poll(&e)) if (s_evn < 32) s_ev[s_evn++] = e.type;
}

static void reset_all(void)
{
    fx_gesture_reset();
    fx_gesture_config_default();
    s_evn = 0;
    collect();
}

static int count_of(fx_gesture_type_t t)
{
    int n = 0;
    for (int i = 0; i < s_evn; i++) if (s_ev[i] == t) n++;
    return n;
}

static void dump(void)
{
    printf("         事件序列(%d 条):", s_evn);
    for (int i = 0; i < s_evn; i++) printf(" %s", fx_gesture_name(s_ev[i]));
    printf("\n");
}

/* ---- 合成手势构造 ---- */

/* 快速点一下, 然后推进 400ms 让双击窗口过去 → 应结算成单击 */
static void do_tap(int x, int y, uint32_t t)
{
    fx_gesture_feed(x, y, 1, t);
    fx_gesture_feed(x, y, 0, t + 60);
    fx_gesture_feed(x, y, 0, t + 400);   /* 时间推进, 结算待定单击 */
}

/* 两次快速点按(间隔 150ms) → 双击 */
static void do_double_tap(int x, int y, uint32_t t)
{
    fx_gesture_feed(x, y, 1, t);
    fx_gesture_feed(x, y, 0, t + 60);
    fx_gesture_feed(x, y, 1, t + 150);       /* 窗口内第二击 */
    fx_gesture_feed(x, y, 0, t + 210);
    fx_gesture_feed(x, y, 0, t + 700);
}

int main(void)
{
    printf("== fxtk v2.5 触摸手势自检 ==\n");

    printf("\n-- ① 单击 --\n");
    reset_all();
    do_tap(100, 100, 1000);
    collect();
    chk("快速按抬 → 产生 1 个单击", count_of(FX_GEST_TAP) == 1);
    chk("单击不误报双击/长按", count_of(FX_GEST_DOUBLE_TAP) == 0 && count_of(FX_GEST_LONG_PRESS) == 0);
    dump();

    printf("\n-- ② 双击 --\n");
    reset_all();
    do_double_tap(100, 100, 2000);
    collect();
    chk("两次快速点按 → 产生 1 个双击", count_of(FX_GEST_DOUBLE_TAP) == 1);
    chk("双击不应再单独报单击", count_of(FX_GEST_TAP) == 0);
    dump();

    printf("\n-- ③ 长按 --\n");
    reset_all();
    fx_gesture_feed(200, 200, 1, 3000);
    fx_gesture_feed(200, 200, 1, 3300);   /* 还没到 500ms */
    collect();
    chk("按住 300ms 尚未报长按", count_of(FX_GEST_LONG_PRESS) == 0);
    fx_gesture_feed(200, 200, 1, 3600);   /* 累计 600ms > 500ms */
    collect();
    chk("按住 600ms → 报 1 个长按", count_of(FX_GEST_LONG_PRESS) == 1);
    fx_gesture_feed(200, 200, 0, 3700);   /* 松手 */
    fx_gesture_feed(200, 200, 0, 4200);
    collect();
    chk("长按后松手【不】顺带触发单击(否则会误点按钮)", count_of(FX_GEST_TAP) == 0);
    dump();

    printf("\n-- ④ 拖拽(慢, 不算滑动) --\n");
    reset_all();
    fx_gesture_feed(100, 100, 1, 5000);
    fx_gesture_feed(140, 100, 1, 5400);   /* 移 40px 但很慢 */
    fx_gesture_feed(180, 100, 1, 5900);   /* 已超过滑动时限 600ms 的累计 */
    fx_gesture_feed(180, 100, 0, 6400);
    collect();
    chk("产生拖拽开始", count_of(FX_GEST_DRAG_BEGIN) >= 1);
    chk("产生拖拽移动", count_of(FX_GEST_DRAG_MOVE) >= 1);
    chk("产生拖拽结束", count_of(FX_GEST_DRAG_END) == 1);
    chk("慢拖不应被误判成滑动", count_of(FX_GEST_SWIPE_LEFT) == 0);
    dump();

    printf("\n-- ⑤ 四向滑动 --\n");
    {
        struct { int dx, dy; fx_gesture_type_t want; const char *nm; } cs[] = {
            { -120,   0, FX_GEST_SWIPE_LEFT,  "左滑" },
            {  120,   0, FX_GEST_SWIPE_RIGHT, "右滑" },
            {    0, -120, FX_GEST_SWIPE_UP,   "上滑" },
            {    0,  120, FX_GEST_SWIPE_DOWN, "下滑" },
        };
        for (int i = 0; i < 4; i++) {
            reset_all();
            uint32_t t = 10000 + (uint32_t)i * 1000;
            fx_gesture_feed(300, 300, 1, t);
            fx_gesture_feed(300 + cs[i].dx / 2, 300 + cs[i].dy / 2, 1, t + 40);
            fx_gesture_feed(300 + cs[i].dx, 300 + cs[i].dy, 1, t + 80);
            fx_gesture_feed(300 + cs[i].dx, 300 + cs[i].dy, 0, t + 100);   /* 100ms 内滑完 */
            collect();
            char nm[96];
            snprintf(nm, sizeof nm, "%s(位移 %d,%d / 100ms) 被识别", cs[i].nm, cs[i].dx, cs[i].dy);
            chk(nm, count_of(cs[i].want) == 1);
        }
    }

    printf("\n-- ⑥ 不该误报的边界 --\n");
    reset_all();
    fx_gesture_feed(50, 50, 1, 20000);
    fx_gesture_feed(50, 50, 0, 20400);    /* 按了 400ms: 超点按时限, 又没到长按 */
    fx_gesture_feed(50, 50, 0, 20800);
    collect();
    chk("按 400ms 松手: 既不是单击也不是长按(不硬凑事件)", s_evn == 0);
    dump();

    reset_all();
    fx_gesture_feed(50, 50, 1, 30000);
    fx_gesture_feed(55, 53, 0, 30080);    /* 手指抖了 5px, 仍应算单击 */
    fx_gesture_feed(50, 50, 0, 30400);
    collect();
    chk("点按时手指抖 5px 仍算单击(抖动容差)", count_of(FX_GEST_TAP) == 1);

    reset_all();
    fx_gesture_feed(50, 50, 0, 40000);    /* 没按过就松 */
    collect();
    chk("没按下就抬起不产生任何事件", s_evn == 0);

    reset_all();
    fx_gesture_feed(10, 10, 1, 50000);
    chk("按下后 fx_gesture_active() 为真", fx_gesture_active() == 1);
    fx_gesture_feed(10, 10, 0, 50060);
    fx_gesture_feed(10, 10, 0, 50400);
    collect();
    chk("抬起并结算后 active() 为假", fx_gesture_active() == 0);

    printf("\n-- ⑦ 队列不丢事件 (while 取空) --\n");
    reset_all();
    {
        /* 一次长拖拽会在多帧产生多条 DRAG_MOVE, 验证都能取到 */
        uint32_t t = 60000;
        fx_gesture_feed(0, 0, 1, t);
        for (int i = 1; i <= 10; i++) fx_gesture_feed(i * 20, 0, 1, t + (uint32_t)i * 20);
        fx_gesture_feed(200, 0, 0, t + 250);
        collect();
        chk("10 次拖拽移动全部收到(不被覆盖)", count_of(FX_GEST_DRAG_MOVE) == 10);
        chk("队列没爆(丢弃数为 0)", fx_gesture_dropped() == 0);
        dump();
    }

    printf("\n== 结果: %s (失败 %d 项) ==\n", s_fail ? "FAIL" : "PASS", s_fail);
    printf("注: 捏合缩放(pinch)/双指旋转【未实现】—— 驱动 touch_read 只给单点,\n");
    printf("    接口给不出第二个触点。要支持须先扩展驱动接口。\n");
    return s_fail ? 1 : 0;
}
