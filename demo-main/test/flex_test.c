/**
 * flex_test.c — v2.5 弹性布局自检 (无头, 只链 fxtk_flex.c)
 *
 * 【为什么布局必须单测】
 *   布局错是最难靠肉眼发现的一类 bug: 差 1px、gap 少数一次、最后一个被挤出容器 ——
 *   看截图基本看不出来, 但用户会一直觉得"哪里不对劲"。
 *   所以这里用【不变量】来验, 而不是比对具体坐标:
 *     ① 主轴尺寸之和 + gap 之和 == 可用主轴空间   (不丢像素、不溢出)
 *     ② 相邻子件【不重叠】且按顺序排列
 *     ③ 所有子件都在容器内容框内(不做收缩的前提下, 只对放得下的用例断言)
 */
#include "fxtk.h"
#include "fxtk_internal.h"   /* v2.5: 应用层测试要在栈上手工搭控件树, 需要完整结构体 */
#include <stdio.h>
#include <string.h>

static int s_fail = 0;

static void chk(const char *name, int ok)
{
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) s_fail++;
}

/* 主轴占用量 = 各子件主轴尺寸 + gap 之和。
 * ⚠️ 注意不变量不是"恒等于可用空间": 只有当存在能吸收剩余空间的项(grow>0 且未到
 *    max_size)时才会占满。全部是固定尺寸时剩余空间由 justify 留白 —— 那是对的。
 *    初版把"占满"当成普遍不变量, 于是把正确的留白行为误判成 FAIL。 */
static int sum_main(int dir, int gap, const int *xy, int n)
{
    int row = (dir == FX_FLEX_ROW);
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += row ? (xy[i*4+2] - xy[i*4+0] + 1) : (xy[i*4+3] - xy[i*4+1] + 1);
    sum += (n > 1) ? gap * (n - 1) : 0;
    return sum;
}

/* 是否存在"能吸收剩余空间"的项 */
static int can_absorb(const fx_flex_item_t *it, int n)
{
    for (int i = 0; i < n; i++)
        if (it[i].grow > 0 && it[i].max_size <= 0) return 1;
    return 0;
}

/* 不变量②: 不重叠且有序 */
static int check_noreorder(int dir, int gap, const int *xy, int n)
{
    int row = (dir == FX_FLEX_ROW);
    for (int i = 1; i < n; i++) {
        int prev_end = row ? xy[(i-1)*4+2] : xy[(i-1)*4+3];
        int cur_beg  = row ? xy[i*4+0]     : xy[i*4+1];
        if (cur_beg <= prev_end) return 0;      /* 重叠或逆序 */
        if (cur_beg - prev_end - 1 < gap) return 0; /* 间距不足 */
    }
    return 1;
}

int main(void)
{
    printf("== fxtk v2.5 弹性布局自检 ==\n");

    printf("\n-- ① grow 按比例分配, 且不丢像素 --\n");
    {
        /* 容器 300 宽, pad 0, gap 0, 三项 grow 1:1:1 → 必须正好 100/100/100 */
        fx_flex_item_t it[3];
        for (int i = 0; i < 3; i++) { it[i].size = 0; it[i].grow = 1;
                                      it[i].min_size = 0; it[i].max_size = 0; it[i].cross_size = 0; }
        int xy[12];
        int n = fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH,
                              it, 3, xy);
        chk("返回 3 项", n == 3);
        chk("三项各 100 宽", (xy[2]-xy[0]+1) == 100 && (xy[6]-xy[4]+1) == 100 && (xy[10]-xy[8]+1) == 100);
        chk("三项 grow 把 300 全部吸收(不丢像素)", sum_main(FX_FLEX_ROW, 0, xy, 3) == 300);
    }

    printf("\n-- ② 除不尽的余数不能丢 (本项目的历史顽疾) --\n");
    {
        /* 100 分给 3 个 grow 1:1:1 → 33/33/34 或 34/33/33, 总和必须正好 100 */
        fx_flex_item_t it[3];
        for (int i = 0; i < 3; i++) { it[i].size = 0; it[i].grow = 1;
                                      it[i].min_size = 0; it[i].max_size = 0; it[i].cross_size = 0; }
        int xy[12];
        fx_flex_solve(0, 0, 99, 49, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 3, xy);
        int w0 = xy[2]-xy[0]+1, w1 = xy[6]-xy[4]+1, w2 = xy[10]-xy[8]+1;
        chk("100 分 3 份后总和仍为 100", w0+w1+w2 == 100);
        printf("         实测 %d + %d + %d = %d\n", w0, w1, w2, w0+w1+w2);
        chk("三者差不超过 1px(分得均匀)", (w0>=33&&w2<=34) && (w0>=33&&w0<=34) && (w2>=33&&w2<=34));
    }

    printf("\n-- ③ 固定尺寸 + grow 混合 --\n");
    {
        /* 300 宽, gap 10, 三项: 固定 60 / 固定 40 / grow1 → 剩余 300-10*2-100=180 给第三项 */
        fx_flex_item_t it[3] = {
            { 60, 0, 0, 0, 0 }, { 40, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0 }
        };
        int xy[12];
        fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 10, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 3, xy);
        chk("固定项保持 60 / 40",
            (xy[2]-xy[0]+1) == 60 && (xy[6]-xy[4]+1) == 40);
        chk("grow 项吃掉全部剩余 = 180", (xy[10]-xy[8]+1) == 180);
        chk("总和 == 可用空间(300)", sum_main(FX_FLEX_ROW, 10, xy, 3) == 300);
        chk("不重叠且间距 >= gap", check_noreorder(FX_FLEX_ROW, 10, xy, 3));
    }

    printf("\n-- ④ min/max 夹紧 --\n");
    {
        fx_flex_item_t it[2] = {
            { 0, 1, 0, 40, 0 },     /* 想长很大, 但 max 40 */
            { 0, 1, 0, 0, 0 }
        };
        int xy[8];
        fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 2, xy);
        chk("max_size=40 生效", (xy[2]-xy[0]+1) == 40);
        chk("被夹住的空间【转给】了另一项(不是被扣走)", (xy[6]-xy[4]+1) == 260);
        chk("总和 == 可用空间(300)", sum_main(FX_FLEX_ROW, 0, xy, 2) == 300);
    }

    printf("\n-- ⑤ justify 五种分布 --\n");
    {
        /* 两个 50 宽固定项, 容器 300 → 剩余 200 由 justify 决定怎么放 */
        fx_flex_item_t it[2] = { { 50, 0, 0, 0, 0 }, { 50, 0, 0, 0, 0 } };
        int xy[8];
        struct { int j; const char *nm; int want_x1; } cs[] = {
            { FX_FLEX_START,         "START 靠左",          0   },
            { FX_FLEX_CENTER,        "CENTER 居中",         100 },
            { FX_FLEX_END,           "END 靠右",            200 },
        };
        for (int i = 0; i < 3; i++) {
            fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 0, cs[i].j, FX_FLEX_ALIGN_STRETCH, it, 2, xy);
            char nm[96]; snprintf(nm, sizeof nm, "%s → 首项 x1=%d (期望 %d)", cs[i].nm, xy[0], cs[i].want_x1);
            chk(nm, xy[0] == cs[i].want_x1);
        }
        /* SPACE_BETWEEN: 首项贴左, 末项贴右 */
        fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 0, FX_FLEX_SPACE_BETWEEN, FX_FLEX_ALIGN_STRETCH, it, 2, xy);
        chk("SPACE_BETWEEN: 首项贴左", xy[0] == 0);
        chk("SPACE_BETWEEN: 末项贴右", xy[2+4] == 299);
        /* SPACE_AROUND: 两侧留白 = 间距/2 */
        fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 0, FX_FLEX_SPACE_AROUND, FX_FLEX_ALIGN_STRETCH, it, 2, xy);
        int left = xy[0], inner = xy[4] - (xy[2]+1);
        chk("SPACE_AROUND: 两边留白相等", left == 299 - xy[6]);
        chk("SPACE_AROUND: 中间间距 = 2×边距", inner == 2 * left);
        printf("         边距=%d 中间=%d\n", left, inner);
    }

    printf("\n-- ⑥ align 四种对齐 --\n");
    {
        fx_flex_item_t it[1] = { { 50, 0, 0, 0, 30 } };   /* 交叉轴固定 30 */
        int xy[4];
        fx_flex_solve(0, 0, 299, 199, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_START, it, 1, xy);
        chk("ALIGN_START → y1=0", xy[1] == 0);
        fx_flex_solve(0, 0, 299, 199, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_CENTER, it, 1, xy);
        chk("ALIGN_CENTER → 垂直居中", xy[1] == (200 - 30) / 2);
        fx_flex_solve(0, 0, 299, 199, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_END, it, 1, xy);
        chk("ALIGN_END → 贴底", xy[3] == 199);
        fx_flex_solve(0, 0, 299, 199, FX_FLEX_ROW, 0, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 1, xy);
        chk("ALIGN_STRETCH → 拉满高(忽略 cross_size)", (xy[3]-xy[1]+1) == 200);
    }

    printf("\n-- ⑦ 列方向 (COL) 与行方向镜像 --\n");
    {
        fx_flex_item_t it[2] = { { 40, 0, 0, 0, 0 }, { 40, 0, 0, 0, 0 } };
        int xyc[8];
        fx_flex_solve(0, 0, 99, 299, FX_FLEX_COL, 10, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 2, xyc);
        chk("COL: 第一个在上方", xyc[1] == 0);
        chk("COL: 第二个在下方(y1=50)", xyc[5] == 50);
        chk("COL: 宽度拉满 100", (xyc[2]-xyc[0]+1) == 100);
        chk("COL: 主轴占用 = 40+10+40 = 90 (等宽固定项, 不占满是对的)", sum_main(FX_FLEX_COL, 10, xyc, 2) == 90);
    }

    printf("\n-- ⑧ pad 内边距 --\n");
    {
        fx_flex_item_t it[1] = { { 0, 1, 0, 0, 0 } };
        int xy[4];
        fx_flex_solve(0, 0, 299, 99, FX_FLEX_ROW, 0, 10, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 1, xy);
        chk("pad=10 → x1=10", xy[0] == 10);
        chk("pad=10 → x2=289", xy[2] == 289);
        chk("可用宽 = 300-20 = 280", (xy[2]-xy[0]+1) == 280);
    }

    printf("\n-- ⑨ 边界: 0 项 / 1 项 / 负容器 / 交叉轴为 0 --\n");
    {
        fx_flex_item_t it[2] = { { 30, 0, 0, 0, 0 }, { 30, 0, 0, 0, 0 } };
        int xy[8];
        chk("n=0 返回 0", fx_flex_solve(0,0,99,99, FX_FLEX_ROW,0,0,0,0, it, 0, xy) == 0);
        chk("items=NULL 返回 0", fx_flex_solve(0,0,99,99, FX_FLEX_ROW,0,0,0,0, NULL, 2, xy) == 0);
        chk("out=NULL 返回 0", fx_flex_solve(0,0,99,99, FX_FLEX_ROW,0,0,0,0, it, 2, NULL) == 0);
        int n1 = fx_flex_solve(0,0,199,99, FX_FLEX_ROW,0,0,FX_FLEX_START,FX_FLEX_ALIGN_STRETCH, it, 1, xy);
        chk("n=1 正常(size=30 → x2=29, STRETCH 只作用于交叉轴)", n1 == 1 && xy[0] == 0 && xy[2] == 29);
        /* 容器坐标反向传入: 应被规范化而不是算出负尺寸 */
        int nr = fx_flex_solve(299,99,0,0, FX_FLEX_ROW,0,0,FX_FLEX_START,FX_FLEX_ALIGN_STRETCH, it, 1, xy);
        chk("反序容器被规范化(x1<=x2)", nr == 1 && xy[0] <= xy[2]);
        /* n 超过 64: 应被截断到 64 且不越界 */
        fx_flex_item_t big[80];
        for (int i = 0; i < 80; i++) { big[i].size=1; big[i].grow=0; big[i].min_size=0; big[i].max_size=0; big[i].cross_size=0; }
        int xybig[80*4];
        int nb = fx_flex_solve(0,0,999,99, FX_FLEX_ROW,0,0,FX_FLEX_START,FX_FLEX_ALIGN_STRETCH, big, 80, xybig);
        chk("n>64 被截断到 64(不越界)", nb == 64);
    }

    printf("\n-- ⑩ 随机穷举: 不溢出 / 有 grow 时恰好占满 / 不重叠 --\n");
    {
        /* 用确定性的伪随机序列跑大量组合, 只断言不变量①和② */
        unsigned seed = 12345u;
        int bad_sum = 0, bad_ord = 0, cases = 0;
        for (int t = 0; t < 3000; t++) {
            seed = seed * 1103515245u + 12345u;
            int w = 50 + (int)(seed >> 16) % 1000;
            seed = seed * 1103515245u + 12345u;
            int n = 1 + (int)(seed >> 16) % 6;
            seed = seed * 1103515245u + 12345u;
            int gap = (int)(seed >> 16) % 12;
            seed = seed * 1103515245u + 12345u;
            int pad = (int)(seed >> 16) % 16;
            seed = seed * 1103515245u + 12345u;
            int justify = (int)(seed >> 16) % 5;

            fx_flex_item_t it[6];
            for (int i = 0; i < n; i++) {
                seed = seed * 1103515245u + 12345u;
                it[i].size = (int)(seed >> 16) % 80;
                seed = seed * 1103515245u + 12345u;
                it[i].grow = (int)(seed >> 16) % 3;
                it[i].min_size = 0; it[i].max_size = 0; it[i].cross_size = 0;
            }
            /* 只在内容放得下时才要求"占满"(不做收缩的边界情况另说) */
            int need = pad * 2 + (n > 1 ? gap * (n - 1) : 0);
            for (int i = 0; i < n; i++) need += it[i].size;
            if (need > w) continue;
            cases++;

            int xy[24];
            int got = fx_flex_solve(0, 0, w - 1, 99, FX_FLEX_ROW, gap, pad, justify, FX_FLEX_ALIGN_STRETCH,
                                    it, n, xy);
            if (got != n) { bad_sum++; continue; }
            int avail_r = w - 2 * pad;
            int used_r = sum_main(FX_FLEX_ROW, gap, xy, n);
            if (used_r > avail_r) bad_sum++;              /* 恒真: 不溢出 */
            else if (can_absorb(it, n) && used_r != avail_r) bad_sum++;  /* 有 grow 必须占满 */
            /* 只对 START 检查相邻间距(其它 justify 会额外插入空隙, 也不会重叠) */
            if (!check_noreorder(FX_FLEX_ROW, gap, xy, n)) bad_ord++;
        }
        printf("         有效用例 %d 组\n", cases);
        chk("3000 组随机中: 主轴不溢出, 且有 grow 时恰好占满", bad_sum == 0);
        chk("3000 组随机中: 子件从不重叠/逆序, 间距 >= gap", bad_ord == 0);
    }

    printf("\n-- ⑪ 应用层 fx_flex_apply: 真的写进控件坐标了吗 --\n");
    {
        /* 在栈上手工搭一棵最小控件树 —— 应用层只读 child/sibling/flags 与矩形,
         * 所以不需要链整个框架, 也就不需要驱动/字体。 */
        fx_widget_t cont, a, b, c;
        memset(&cont, 0, sizeof cont); memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);       memset(&c, 0, sizeof c);

        cont.x1 = 0; cont.y1 = 0; cont.x2 = 299; cont.y2 = 99;
        cont.child = &a;
        a.sibling = &b; a.parent = &cont;
        b.sibling = &c; b.parent = &cont;
        c.sibling = NULL; c.parent = &cont;

        a.flags = FX_F_VISIBLE; b.flags = FX_F_VISIBLE; c.flags = 0;  /* c 不可见 */
        a.x1=0; a.y1=0; a.x2=59;  a.y2=29;    /* 自然尺寸 60x30 */
        b.x1=0; b.y1=0; b.x2=39;  b.y2=19;    /* 自然尺寸 40x20 */
        c.x1=0; c.y1=0; c.x2=999; c.y2=999;   /* 不可见, 不应被排 */
        a.pos_mode = b.pos_mode = c.pos_mode = FX_POS_PIXEL;

        fx_flex_apply_simple(&cont, FX_FLEX_ROW, 10, 5, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH);

        chk("可见子件 a 被写入坐标 (x1=5 起, pad=5)", a.x1 == 5);
        chk("a 保持自然宽 60", (a.x2 - a.x1 + 1) == 60);
        chk("b 紧接 a + gap 10 → x1 = 5+60+10 = 75", b.x1 == 75);
        chk("b 保持自然宽 40", (b.x2 - b.x1 + 1) == 40);
        chk("ALIGN_STRETCH → a/b 交叉轴拉满 容器高-2*pad", (a.y2-a.y1+1) == 90 && (b.y2-b.y1+1) == 90);
        chk("不可见子件 c 未被改动(仍是原值)", c.x1 == 0 && c.x2 == 999);
        chk("a/b 被标记 FX_POS_FLEX(布局期不再覆盖)", a.pos_mode == FX_POS_FLEX && b.pos_mode == FX_POS_FLEX);
        chk("不可见的 c 保持原 pos_mode", c.pos_mode == FX_POS_PIXEL);

        /* 用 grow 权重: 固定 60 / 固定 40 / grow1 吃掉剩余 */
        fx_widget_t d;
        memset(&d, 0, sizeof d);
        d.flags = FX_F_VISIBLE;
        b.sibling = &d; d.parent = &cont; d.sibling = NULL;
        a.x1=0; a.x2=59; b.x1=0; b.x2=39;
        fx_flex_item_t it[3] = { {60,0,0,0,0}, {40,0,0,0,0}, {0,1,0,0,0} };
        fx_flex_apply(&cont, it, 3, FX_FLEX_ROW, 10, 0, FX_FLEX_START, FX_FLEX_ALIGN_STRETCH);
        chk("三子件: a=60 b=40", (a.x2-a.x1+1)==60 && (b.x2-b.x1+1)==40);
        chk("第三个 grow 项吃掉剩余 180", (d.x2-d.x1+1)==180);
        chk("总宽恰好 300 (60+10+40+10+180)", (d.x2 - a.x1 + 1) == 300);
    }

    printf("\n== 结果: %s (失败 %d 项) ==\n", s_fail ? "FAIL" : "PASS", s_fail);
    printf("注: 本模块【不做收缩、不换行】—— 空间不够时按基础尺寸溢出, 不压缩子件。\n");
    return s_fail ? 1 : 0;
}
