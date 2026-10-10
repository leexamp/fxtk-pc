/**
 * fxtk_flex.c — v2.5: 弹性布局 (flex 子集)
 *
 * 【设计原则: 求解与应用分离】
 *   fx_flex_solve() 是【纯函数】: 只吃几何 + 尺寸, 只吐坐标, 不碰任何控件与驱动。
 *   好处是它可以被穷举单测(见 demo-main/test/flex_test.c) —— 布局算错是最难靠肉眼
 *   发现的一类 bug(差 1px、最后一个被挤出屏幕、gap 少数一次), 靠看截图基本看不出来。
 *
 * 【为什么不用 C 整数除法直接分 grow】
 *   剩余空间 100 分给 grow 1:1:1 → 33/33/33 还剩 1px, 谁都不管就少 1px。
 *   本项目布局到处差 1px 是历史顽疾, 所以这里用"余数按顺序补给前几个"的方式,
 *   保证【分出去的总和恰好等于可用空间】(有单测守着这条)。
 *
 * 【诚实边界】
 *   ① 不做收缩: 可用空间不够时【不压缩】子件, 直接按基础尺寸铺出去让它溢出。
 *      真正的 flex-shrink 需要一套优先级/下限规则, 且与"clamp 到屏幕内"的历史行为
 *      容易打架; 这版先不做, 也不假装做了。
 *   ② 不做换行(flex-wrap)。超出就超出。
 *   ③ 不做嵌套 flex 的自动尺寸推导: 容器尺寸由调用方给(通常用 percent/pixel 定),
 *      子件的基础尺寸由调用方给(或用 fx_flex_apply_simple 取控件当前矩形)。
 */
#include "fxtk.h"
#include "fxtk_internal.h"

/* ===================== 纯求解器 ===================== */

int fx_flex_solve(int cx1, int cy1, int cx2, int cy2,
                  int dir, int gap, int pad, int justify, int align,
                  const fx_flex_item_t *items, int n,
                  int *out_xy4)
{
    if (!items || !out_xy4 || n <= 0) return 0;
    if (cx2 < cx1) { int t = cx1; cx1 = cx2; cx2 = t; }
    if (cy2 < cy1) { int t = cy1; cy1 = cy2; cy2 = t; }
    if (pad < 0) pad = 0;
    if (gap < 0) gap = 0;

    int row = (dir == FX_FLEX_ROW);
    int box_x1 = cx1 + pad, box_y1 = cy1 + pad;
    int box_x2 = cx2 - pad, box_y2 = cy2 - pad;
    if (box_x2 < box_x1) box_x2 = box_x1;
    if (box_y2 < box_y1) box_y2 = box_y1;

    int avail_main = (row ? (box_x2 - box_x1 + 1) : (box_y2 - box_y1 + 1));
    int avail_cross = (row ? (box_y2 - box_y1 + 1) : (box_x2 - box_x1 + 1));

    int total_gap = (n > 1) ? gap * (n - 1) : 0;
    int free_main = avail_main - total_gap;

    /* 迭代求解: 每轮把【尚未分配】的空间按 grow 分给"还能长"的项, 然后夹紧;
     * 夹紧会改变"尚未分配"的量, 所以再跑一轮。最多 4 轮, 收敛即可。
     *
     * 旧写法是"先一次性分完再夹紧, 夹紧产生的差额从别的项里扣" —— 那只在
     * 差额为【正】时成立; 某项被 max 夹【小】时差额是负的, 于是新分配逻辑会从
     * 本应拿更多空间的另一项里倒扣, 结果总和不足。单测用例④正是这样把它抓出来的。 */
    int sz[64];
    if (n > 64) n = 64;   /* 防御: 超过 64 项不再细分(容器内极少有这么多直接子件) */
    for (int i = 0; i < n; i++) sz[i] = (items[i].size > 0) ? items[i].size : 0;

    for (int pass = 0; pass < 4; pass++) {
        long used = 0;
        for (int i = 0; i < n; i++) used += sz[i];
        int room = free_main - (int)used;

        if (room > 0) {
            int w[64], wsum = 0;
            for (int i = 0; i < n; i++) {
                int hi = items[i].max_size > 0 ? items[i].max_size : 0;
                int can = (items[i].grow > 0) && (hi == 0 || sz[i] < hi);
                w[i] = can ? items[i].grow : 0;
                wsum += w[i];
            }
            if (wsum > 0) {
                int given = 0;
                for (int i = 0; i < n; i++) {
                    if (!w[i]) continue;
                    int s = (int)((long)room * w[i] / wsum);
                    sz[i] += s; given += s;
                }
                int rest = room - given;          /* 余数补给前面的项, 保证一像素不丢 */
                for (int i = 0; i < n && rest > 0; i++) if (w[i]) { sz[i]++; rest--; }
            }
        }

        int changed = 0;
        for (int i = 0; i < n; i++) {
            int lo = items[i].min_size > 0 ? items[i].min_size : 0;
            int hi = items[i].max_size > 0 ? items[i].max_size : 0;   /* 0 = 不限制 */
            if (sz[i] < lo) { sz[i] = lo; changed = 1; }
            else if (hi > 0 && sz[i] > hi) { sz[i] = hi; changed = 1; }
        }
        if (!changed) break;
    }

    long total = 0;
    for (int i = 0; i < n; i++) total += sz[i];
    total += total_gap;

    /* 主轴起点: 按 justify 决定 */
    int start = (row ? box_x1 : box_y1);
    int slack = avail_main - (int)total;
    if (slack < 0) slack = 0;
    int extra_gap = 0;
    switch (justify) {
    case FX_FLEX_CENTER:
        start += slack / 2; break;
    case FX_FLEX_END:
        start += slack; break;
    case FX_FLEX_SPACE_BETWEEN:
        if (n > 1) { extra_gap = slack / (n - 1); start += 0; }
        break;
    case FX_FLEX_SPACE_AROUND:
        if (n > 0) { extra_gap = slack / n; start += extra_gap / 2; }
        break;
    default: break;   /* FX_FLEX_START */
    }

    int pos = start;
    for (int i = 0; i < n; i++) {
        int m = sz[i];

        /* 交叉轴: 按 align 决定位置与厚度 */
        int cross_len = items[i].cross_size > 0 ? items[i].cross_size : avail_cross;
        if (cross_len > avail_cross) cross_len = avail_cross;
        int cross_pos = (row ? box_y1 : box_x1);
        switch (align) {
        case FX_FLEX_ALIGN_CENTER: cross_pos += (avail_cross - cross_len) / 2; break;
        case FX_FLEX_ALIGN_END:    cross_pos += (avail_cross - cross_len); break;
        case FX_FLEX_ALIGN_STRETCH:cross_len = avail_cross; break;
        default: break;            /* START */
        }

        int a1 = pos, a2 = pos + m - 1;
        int b1 = cross_pos, b2 = cross_pos + cross_len - 1;
        if (row) { out_xy4[i*4+0] = a1; out_xy4[i*4+1] = b1; out_xy4[i*4+2] = a2; out_xy4[i*4+3] = b2; }
        else     { out_xy4[i*4+0] = b1; out_xy4[i*4+1] = a1; out_xy4[i*4+2] = b2; out_xy4[i*4+3] = a2; }

        pos = a2 + 1 + gap + extra_gap;
    }
    return n;
}

/* ===================== 应用层 ===================== */

/* 收集容器的可见子件, 求解后直接写入它们的矩形(并标记 FX_POS_FLEX, 让布局期不再覆盖) */
void fx_flex_apply(fx_widget_t *container,
                   const fx_flex_item_t *items, int n,
                   int dir, int gap, int pad, int justify, int align)
{
    if (!container || !items || n <= 0) return;

    int xy[64 * 4];
    int got = fx_flex_solve(container->x1, container->y1, container->x2, container->y2,
                            dir, gap, pad, justify, align, items, n, xy);
    if (got <= 0) return;

    int i = 0;
    for (fx_widget_t *c = container->child; c && i < got; c = c->sibling) {
        if (!(c->flags & FX_F_VISIBLE)) continue;   /* 只排可见的 */
        c->x1 = (int16_t)xy[i*4+0]; c->y1 = (int16_t)xy[i*4+1];
        c->x2 = (int16_t)xy[i*4+2]; c->y2 = (int16_t)xy[i*4+3];
        c->pos_mode = FX_POS_FLEX;   /* 布局期不重算 */
        i++;
    }
}

/* 常用形态: 每个子件用【当前矩形尺寸】当主轴基础尺寸, grow=0(不伸缩) */
void fx_flex_apply_simple(fx_widget_t *container, int dir, int gap, int pad,
                          int justify, int align)
{
    if (!container) return;
    fx_flex_item_t it[64];
    int n = 0;
    int row = (dir == FX_FLEX_ROW);
    for (fx_widget_t *c = container->child; c && n < 64; c = c->sibling) {
        if (!(c->flags & FX_F_VISIBLE)) continue;
        it[n].size       = row ? (c->x2 - c->x1 + 1) : (c->y2 - c->y1 + 1);
        it[n].grow       = 0;
        it[n].min_size   = 0;
        it[n].max_size   = 0;
        it[n].cross_size = row ? (c->y2 - c->y1 + 1) : (c->x2 - c->x1 + 1);
        n++;
    }
    fx_flex_apply(container, it, n, dir, gap, pad, justify, align);
}
