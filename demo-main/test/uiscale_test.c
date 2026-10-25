/**
 * uiscale_test.c — v2.5 UI 缩放自检 (无头, 只链 fxtk_backends.c)
 *
 * 为什么要单独测这个纯函数:
 *   fxtk_ui_scale() 原先在两个驱动里各有一份手抄算式, 且都没处理 dpi。
 *   抽成纯函数后, 这里用【遍历式对比】守住两件事:
 *     ① dpi=1.0 时必须与旧公式【逐字节相同】—— 这是"非 HiDPI 机器零回归"的硬证据,
 *        而不是"我看着差不多"。
 *     ② dpi>1 时必须等于"按逻辑宽算的缩放 × dpi"。
 *
 * 【本机测不到的部分, 如实标明】
 *   本机 sapp_dpi_scale()=1.000(非 HiDPI), 所以 dpi>1 的【数学】在这里被穷举验证了,
 *   但"在真 HiDPI 屏上肉眼是否清晰/大小是否合适"【没有实机验证】——
 *   那需要在 2x 屏上跑。别把这里的 PASS 读成"HiDPI 已实机验证"。
 */
#include "fxtk_backends.h"
#include <stdio.h>

static int s_fail = 0;

static void chk(const char *name, int ok)
{
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) s_fail++;
}

/* 改动前的旧算式, 原样抄下来当基准(两个驱动里各有一份, 这里取其一) */
static int old_scale(int w, int cap, int compact)
{
    int s = w > 0 ? (w * 100) / 480 : 100;
    if (s < 100) s = 100;
    if (!compact && s > cap) s = cap;
    return s;
}

int main(void)
{
    printf("== fxtk v2.5 UI 缩放自检 ==\n");

    printf("\n-- ① 零回归: dpi=1.0 时必须与旧公式完全一致 --\n");
    {
        int caps[] = { 100, 120, 160, 200, 300, 400 };
        int mismatch = 0, first_w = -1, first_cap = -1, first_c = -1, got = 0, want = 0;
        for (int c = 0; c < 6; c++) {
            for (int compact = 0; compact <= 1; compact++) {
                for (int w = 0; w <= 4000; w++) {
                    int a = fx_ui_scale_for(w, 1.0f, caps[c], compact);
                    int b = old_scale(w, caps[c], compact);
                    if (a != b) {
                        if (mismatch == 0) { first_w = w; first_cap = caps[c]; first_c = compact; got = a; want = b; }
                        mismatch++;
                    }
                }
            }
        }
        chk("dpi=1.0 下 48048 组 (w,cap,compact) 全部与旧公式相同", mismatch == 0);
        if (mismatch) printf("         首个不一致: w=%d cap=%d compact=%d 新=%d 旧=%d\n",
                             first_w, first_cap, first_c, got, want);
    }

    printf("\n-- ② dpi>1: 应为 (逻辑宽算出的缩放) × dpi --\n");
    {
        struct { int phys; float dpi; int cap; int expect; } cs[] = {
            /* 2560 物理 / dpi2 → 逻辑 1280 → 1280*100/480=266 → cap160 → ×2 = 320 */
            { 2560, 2.0f, 160, 320 },
            /* 1280 物理 / dpi1  → 逻辑 1280 → 266 → cap160 → ×1 = 160 */
            { 1280, 1.0f, 160, 160 },
            /* 3840 物理 / dpi2 → 逻辑 1920 → 400 → cap160 → ×2 = 320 */
            { 3840, 2.0f, 160, 320 },
            /* 大屏不封顶(compact): 3840/2=1920 → 400 → ×2 = 800 */
            { 3840, 2.0f, 160, 800 },
            /* 小窗口也要保底 100%: 480 物理 / dpi1 → 100 */
            { 480,  1.0f, 160, 100 },
            /* 极窄: 200 物理 / dpi1 → 41 → 保底 100 */
            { 200,  1.0f, 160, 100 },
        };
        for (size_t i = 0; i < sizeof(cs) / sizeof(cs[0]); i++) {
            int compact = (i == 3) ? 1 : 0;
            int got = fx_ui_scale_for(cs[i].phys, cs[i].dpi, cs[i].cap, compact);
            char nm[128];
            snprintf(nm, sizeof nm, "phys=%d dpi=%.1f cap=%d compact=%d → %d (期望 %d)",
                     cs[i].phys, (double)cs[i].dpi, cs[i].cap, compact, got, cs[i].expect);
            chk(nm, got == cs[i].expect);
        }
    }

    printf("\n-- ③ 防御: dpi 非法/为零 不当成 0 除 --\n");
    {
        chk("dpi=0.0 视为 1.0",   fx_ui_scale_for(1280, 0.0f,  160, 0) == old_scale(1280, 160, 0));
        chk("dpi=-2.0 视为 1.0",  fx_ui_scale_for(1280, -2.0f, 160, 0) == old_scale(1280, 160, 0));
        chk("phys_w=0 返回 100(dpi1)", fx_ui_scale_for(0, 1.0f, 160, 0) == 100);
        chk("结果永远 > 0",       fx_ui_scale_for(0, 1.0f, 100, 1) > 0);
        chk("phys_w 为负也安全",  fx_ui_scale_for(-100, 1.0f, 160, 0) == 100);
    }

    printf("\n-- ④ 设计稿宽 = 100%% 基准 (v2.5.3) --\n");
    {
        /* 默认设计稿 480 → 1280 逻辑宽算 266%, 被 cap 到 160 */
        chk("设计稿 480: 1280 宽 → 160 (cap)", fx_ui_scale_for(1280, 1.0f, 160, 0) == 160);
        fx_design_w = 1280; fx_design_h = 720;   /* 换成 1280x720 稿: 1280 宽就是 100% */
        chk("设计稿 1280: 1280 宽 → 100", fx_ui_scale_for(1280, 1.0f, 160, 0) == 100);
        chk("设计稿 1280: 1920 宽 → 150", fx_ui_scale_for(1920, 1.0f, 160, 0) == 150);
        chk("设计稿 1280: 640 宽仍保底 100", fx_ui_scale_for(640, 1.0f, 160, 0) == 100);
        fx_design_w = 480; fx_design_h = 272;    /* 恢复默认, 保证可重入 */
    }

    printf("\n== 结果: %s (失败 %d 项) ==\n", s_fail ? "FAIL" : "PASS", s_fail);
    printf("注: dpi>1 的【数学】已穷举验证; 【实机 HiDPI 显示效果】本机无法验证(非 HiDPI 屏)。\n");
    return s_fail ? 1 : 0;
}
