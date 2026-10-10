/**
 * fxtk_gesture.c — v2.5: 触摸手势识别 (单击/双击/长按/拖拽/滑动)
 *
 * 【为什么是纯状态机 + feed() 而不是直接读驱动】
 *   识别逻辑一旦和"取驱动的 touch_read"耦合, 就只能靠真触摸屏测 ——
 *   而真触摸屏测手势几乎测不准(时序、抖动、坐标精度都不可控)。
 *   拆成"喂样本 → 吐事件"的纯函数后, 可以用合成序列做【完全确定性】的无头测试
 *   (见 demo-main/test/gesture_test.c): 想验"长按 600ms 报不报"就喂 600ms,
 *   不依赖任何硬件、不依赖真实时钟。
 *
 * 【单指 vs 多指 —— 这里如实说明边界】
 *   当前驱动的触摸接口是 touch_read(int*x,int*y,int*pressed), 【只有单点】。
 *   所以捏合缩放(pinch)/旋转这类【双指】手势无法在这里实现 —— 不是不想做,
 *   是接口给不出第二个触点。要做必须先扩展驱动接口(多返回一组坐标+触点 id)。
 *   本模块只做单指手势, 不假装支持双指。
 *
 * 【事件是"本帧累积、逐条取走"】
 *   fx_gesture_poll() 每次取走一条; 一帧内可能产生多条(例如长按后松手再滑),
 *   所以要用 while 取空:
 *       while (fx_gesture_poll(&ev)) { ... }
 */
#include "fxtk.h"
#include <string.h>
#include <math.h>

/* ===================== 默认阈值 (可用 fx_gesture_config 改) ===================== */

static int g_long_press_ms  = 500;   /* 按住多久算长按 */
static int g_tap_max_ms     = 300;   /* 按下-抬起多久内算"点" */
static int g_tap_max_dist   = 12;    /* 点按允许的位移(像素, 手指会抖) */
static int g_swipe_min_dist = 40;    /* 滑动最小位移 */
static int g_swipe_max_ms   = 600;   /* 滑动最大耗时(超过就是慢慢拖, 不是滑) */
static int g_double_tap_ms  = 300;   /* 两次点按间隔上限 */
static int g_drag_thresh    = 12;    /* 超过它就进入拖拽态 */

/* ===================== 状态 ===================== */

typedef enum { GS_IDLE = 0, GS_DOWN, GS_DRAGGING, GS_LONG_SENT } gstate_t;

static gstate_t g_st = GS_IDLE;
static int      g_x0, g_y0;          /* 按下点 */
static int      g_lx, g_ly;          /* 最近位置 */
static uint32_t g_t0;                /* 按下时刻 */
static int      g_pending_tap = 0;   /* 有一次"待定"的单击(在等双击窗口) */
static uint32_t g_last_tap_t = 0;

/* 事件队列: 一帧内最多攒这么多条, 超出丢弃最新的(而不是覆盖旧的 —— 旧事件更先发生) */
#define GQ_MAX 16
static fx_gesture_ev_t g_q[GQ_MAX];
static int g_qn = 0;

static int  s_dbg_dropped = 0;

static void emit(fx_gesture_type_t t, int x, int y, int dx, int dy, int dur)
{
    if (g_qn >= GQ_MAX) { s_dbg_dropped++; return; }
    fx_gesture_ev_t *e = &g_q[g_qn++];
    e->type = t; e->x = x; e->y = y; e->dx = dx; e->dy = dy; e->duration_ms = dur;
}

static int idist(int dx, int dy)
{
    /* 整数平方根近似: 避免每帧调 sqrt; 手势阈值对精确度不敏感 */
    double d = sqrt((double)dx * dx + (double)dy * dy);
    return (int)(d + 0.5);
}

static fx_gesture_type_t swipe_dir(int dx, int dy)
{
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ax >= ay) return dx < 0 ? FX_GEST_SWIPE_LEFT : FX_GEST_SWIPE_RIGHT;
    return dy < 0 ? FX_GEST_SWIPE_UP : FX_GEST_SWIPE_DOWN;
}

/* 到点把这个"待定的单击"落实成 TAP —— 双击窗口内没有第二击就报单击 */
static void flush_pending_tap(uint32_t now)
{
    if (g_pending_tap && (int)(now - g_last_tap_t) > g_double_tap_ms) {
        emit(FX_GEST_TAP, g_lx, g_ly, 0, 0, 0);
        g_pending_tap = 0;
    }
}

/* ===================== 对外 API ===================== */

void fx_gesture_reset(void)
{
    g_st = GS_IDLE;
    g_pending_tap = 0;
    g_qn = 0;
    g_x0 = g_y0 = g_lx = g_ly = 0;
    g_t0 = g_last_tap_t = 0;
}

void fx_gesture_config(int long_press_ms, int tap_max_ms, int tap_max_dist,
                       int swipe_min_dist, int double_tap_ms)
{
    if (long_press_ms  > 0) g_long_press_ms  = long_press_ms;
    if (tap_max_ms     > 0) g_tap_max_ms     = tap_max_ms;
    if (tap_max_dist   > 0) g_tap_max_dist   = tap_max_dist;
    if (swipe_min_dist > 0) g_swipe_min_dist = swipe_min_dist;
    if (double_tap_ms  > 0) g_double_tap_ms  = double_tap_ms;
}

void fx_gesture_config_default(void)
{
    g_long_press_ms  = 500;
    g_tap_max_ms     = 300;
    g_tap_max_dist   = 12;
    g_swipe_min_dist = 40;
    g_swipe_max_ms   = 600;
    g_double_tap_ms  = 300;
    g_drag_thresh    = 12;
}

/* 喂一个触摸样本。t 是毫秒时基(调用方给 fx_time_ms())。
 * 注意: 即使手指没动、没有事件产生, 也要【每帧都喂】——
 *       双击窗口的结算依赖"时间在走"(见 flush_pending_tap)。 */
void fx_gesture_feed(int x, int y, int pressed, uint32_t now_ms)
{
    /* 先结算已到期的"待定单击" */
    flush_pending_tap(now_ms);

    if (pressed) {
        if (g_st == GS_IDLE) {
            g_st = GS_DOWN;
            g_x0 = g_lx = x; g_y0 = g_ly = y; g_t0 = now_ms;
            return;
        }
        /* 已在按: 判长按 / 判拖拽 */
        int dx = x - g_x0, dy = y - g_y0;
        int dist = idist(dx, dy);
        int held = (int)(now_ms - g_t0);

        if (g_st == GS_DOWN) {
            if (dist > g_drag_thresh) {
                g_st = GS_DRAGGING;
                emit(FX_GEST_DRAG_BEGIN, x, y, dx, dy, held);
            } else if (held >= g_long_press_ms) {
                g_st = GS_LONG_SENT;
                emit(FX_GEST_LONG_PRESS, x, y, dx, dy, held);
            }
        }
        if (g_st == GS_DRAGGING) {
            emit(FX_GEST_DRAG_MOVE, x, y, dx, dy, held);
        }
        g_lx = x; g_ly = y;
        return;
    }

    /* 抬起 */
    if (g_st == GS_IDLE) return;

    int dx = g_lx - g_x0, dy = g_ly - g_y0;
    int dist = idist(dx, dy);
    int dur = (int)(now_ms - g_t0);

    if (g_st == GS_DRAGGING) {
        /* 又远又快 → 滑动; 否则只是拖拽结束 */
        if (dist >= g_swipe_min_dist && dur <= g_swipe_max_ms)
            emit(swipe_dir(dx, dy), g_lx, g_ly, dx, dy, dur);
        emit(FX_GEST_DRAG_END, g_lx, g_ly, dx, dy, dur);
    } else if (g_st == GS_LONG_SENT) {
        /* 长按已报过, 松手不再报单击(否则长按会顺带触发按钮) */
    } else { /* GS_DOWN: 没怎么动 */
        if (dist <= g_tap_max_dist && dur <= g_tap_max_ms) {
            if (g_pending_tap && (int)(now_ms - g_last_tap_t) <= g_double_tap_ms) {
                emit(FX_GEST_DOUBLE_TAP, g_lx, g_ly, 0, 0, dur);
                g_pending_tap = 0;
            } else {
                /* 先挂起, 等窗口过去再定单击还是双击 */
                g_pending_tap = 1;
                g_last_tap_t = now_ms;
            }
        }
    }
    g_st = GS_IDLE;
}

int fx_gesture_poll(fx_gesture_ev_t *ev)
{
    if (g_qn <= 0) return 0;
    if (ev) *ev = g_q[0];
    for (int i = 1; i < g_qn; i++) g_q[i - 1] = g_q[i];
    g_qn--;
    return 1;
}

int fx_gesture_pending(void) { return g_qn; }
int fx_gesture_dropped(void) { return s_dbg_dropped; }
int fx_gesture_active(void)  { return g_st != GS_IDLE; }

const char *fx_gesture_name(int t)
{
    switch (t) {
    case FX_GEST_NONE:         return "无";
    case FX_GEST_TAP:          return "单击";
    case FX_GEST_DOUBLE_TAP:   return "双击";
    case FX_GEST_LONG_PRESS:   return "长按";
    case FX_GEST_DRAG_BEGIN:   return "拖拽开始";
    case FX_GEST_DRAG_MOVE:    return "拖拽移动";
    case FX_GEST_DRAG_END:     return "拖拽结束";
    case FX_GEST_SWIPE_LEFT:   return "左滑";
    case FX_GEST_SWIPE_RIGHT:  return "右滑";
    case FX_GEST_SWIPE_UP:     return "上滑";
    case FX_GEST_SWIPE_DOWN:   return "下滑";
    default:                   return "?";
    }
}
