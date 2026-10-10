/**
 * v25_evidence.c — v2.5 无头证据收集 (打印, 不画图)
 *
 * 视觉功能看 PNG; 算力/事件/数学这类功能没有画面, 只好把【可验证的数字】打出来。
 * 每一条都是可复算的, 不是"我保证实现过"。
 */
#include "fxtk.h"
#include "fxtk_backends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ---- 文字/驱动桩 (无头环境没有字体与窗口) ---- */
void fx_draw_text(int x,int y,const char *s){(void)x;(void)y;(void)s;}
void fx_draw_text_c(int x,int y,const char *s,fx_color_t fg,fx_color_t bg){(void)x;(void)y;(void)s;(void)fg;(void)bg;}
void fx_draw_text_c_n(int x,int y,const char *s,int n,fx_color_t fg,fx_color_t bg){(void)x;(void)y;(void)s;(void)n;(void)fg;(void)bg;}
int  fx_text_width(const char *s){(void)s;return 0;}
int  fx_text_width_n(const char *s,int n){(void)s;(void)n;return 0;}
void fxtk_draw_text_size(int size,int x,int y,const char *t,fx_color_t fg,fx_color_t bg){(void)size;(void)x;(void)y;(void)t;(void)fg;(void)bg;}
int  fxtk_text_width_size(int size,const char *t){(void)size;(void)t;return 0;}
void *fxtk_font_size(int size){(void)size;return NULL;}
int  fxtk_font_height(int size){(void)size;return 0;}
void fxtk_font_set_size(int size){(void)size;}
int  fxtk_fps(void){return 60;}
int  fxtk_shift_down(void){return 0;}
int  fxtk_right_click(int *x,int *y){(void)x;(void)y;return 0;}
int  fxtk_ui_scale(void){return 100;}

/* ---- 并行 for: 并行结果必须与串行逐元素相同 ---- */
#define N 200000
static int s_par[N], s_ser[N];

static void par_body(int lo, int hi, void *ud)
{
    (void)ud;
    for (int i = lo; i < hi; i++) s_par[i] = (i * 2654435761u) % 1000003;
}

int main(void)
{
    printf("========== v2.5 无头证据 ==========\n\n");

    /* ---------- ① 并行 for ---------- */
    printf("-- ① 并行 for (fx_parallel_for) --\n");
    printf("   逻辑核数 fx_cpu_count()      = %d\n", fx_cpu_count());
    printf("   并行线程数 fx_parallel_threads() = %d  (0 表示自动)\n",
           fx_parallel_threads());

    for (int i = 0; i < N; i++) s_ser[i] = (i * 2654435761u) % 1000003;
    memset(s_par, 0, sizeof s_par);
    fx_parallel_for(N, par_body, NULL);

    int diff = 0;
    for (int i = 0; i < N; i++) if (s_par[i] != s_ser[i]) diff++;
    printf("   N = %d 个元素\n", N);
    printf("   并行 vs 串行: 不同的元素数 = %d  →  %s\n",
           diff, diff == 0 ? "✅ 逐元素相同" : "❌ 不一致!");
    int nonzero = 0;
    for (int i = 0; i < N; i++) if (s_par[i]) nonzero++;
    printf("   并行结果非零元素 = %d  (说明确实算过, 不是全 0)\n", nonzero);

    /* 强制串行再跑一次, 结果必须仍然相同 */
    fx_parallel_set_threads(1);
    memset(s_par, 0, sizeof s_par);
    fx_parallel_for(N, par_body, NULL);
    diff = 0;
    for (int i = 0; i < N; i++) if (s_par[i] != s_ser[i]) diff++;
    printf("   强制串行(threads=1) vs 串行: 不同 = %d  →  %s\n",
           diff, diff == 0 ? "✅ 相同" : "❌ 不一致!");
    fx_parallel_set_threads(0);

    /* ---------- ② 触摸手势 ---------- */
    printf("\n-- ② 触摸手势 (fx_gesture_*, 单指) --\n");
    printf("   事件类型表:\n");
    for (int t = 0; t <= FX_GEST_SWIPE_DOWN; t++)
        printf("     %d = %s\n", t, fx_gesture_name(t));

    struct { const char *name; int x0,y0,x1,y1; unsigned ms; } cases[] = {
        {"单击",       50, 50,  50, 50,  50},
        {"长按",       50, 50,  50, 50, 900},
        {"上滑",       50,150,  50, 50, 200},
        {"下滑",       50, 50,  50,150, 200},
        {"左滑",      150, 50,  50, 50, 200},
        {"右滑",       50, 50, 150, 50, 200},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        fx_gesture_reset();
        unsigned t = 0;
        fx_gesture_feed(cases[i].x0, cases[i].y0, 1, t);
        t += cases[i].ms;
        fx_gesture_feed(cases[i].x1, cases[i].y1, 1, t);
        t += 16;
        fx_gesture_feed(cases[i].x1, cases[i].y1, 0, t);
        /* 双击窗口要再等一会儿才判定单击 */
        fx_gesture_feed(cases[i].x1, cases[i].y1, 0, t + 400);
        printf("   %-8s → ", cases[i].name);
        fx_gesture_ev_t ev; int got = 0;
        while (fx_gesture_poll(&ev)) { printf("%s ", fx_gesture_name(ev.type)); got++; }
        if (!got) printf("(无事件)");
        printf("\n");
    }

    /* ---------- ③ 高 DPI 缩放 ---------- */
    printf("\n-- ③ 高 DPI 缩放 (fx_ui_scale_for) --\n");
    printf("   %-8s %-6s %-8s %s\n", "物理宽", "dpi", "cap", "缩放%");
    int ws[] = { 480, 960, 1280, 1920, 2560, 3840 };
    for (size_t i = 0; i < sizeof(ws)/sizeof(ws[0]); i++)
        printf("   %-8d %-6.1f %-8d %d\n", ws[i], 1.0f, 100,
               fx_ui_scale_for(ws[i], 1.0f, 100, 0));
    printf("   --- dpi=2.0 (2 倍屏, 本机没有, 只能看数学) ---\n");
    for (size_t i = 0; i < sizeof(ws)/sizeof(ws[0]); i++)
        printf("   %-8d %-6.1f %-8d %d\n", ws[i], 2.0f, 100,
               fx_ui_scale_for(ws[i], 2.0f, 100, 0));

    /* ---------- ④ 弹性布局 ---------- */
    printf("\n-- ④ 弹性布局 (fx_flex_solve 纯函数) --\n");
    {
        fx_flex_item_t it[3] = { {60,0,0,0,0}, {80,0,0,0,0}, {40,1,0,0,0} };
        int xy[12];
        int got = fx_flex_solve(0, 0, 479, 271, FX_FLEX_COL, 6, 6,
                                FX_FLEX_START, FX_FLEX_ALIGN_STRETCH, it, 3, xy);
        printf("   容器 480x272, gap=6 pad=6, 基础高 60/80/40, 第三块 grow=1\n");
        printf("   解出 %d 个:\n", got);
        for (int i = 0; i < got; i++)
            printf("     [%d] x %d..%d (宽 %d)   y %d..%d (高 %d)\n", i,
                   xy[i*4+0], xy[i*4+2], xy[i*4+2]-xy[i*4+0]+1,
                   xy[i*4+1], xy[i*4+3], xy[i*4+3]-xy[i*4+1]+1);
        int total = xy[2*4+3] - xy[0*4+1] + 1;
        printf("   三块总高 + 2 个 gap = %d  (容器内容高 = 272-12 = 260)\n",
               total + 12);
    }

    /* ---------- ⑤ SVG ---------- */
    printf("\n-- ⑤ SVG 解析 --\n");
    {
        struct { const char *n; const char *s; } docs[] = {
            {"矩形",  "<svg viewBox=\"0 0 10 10\"><rect x=\"0\" y=\"0\" width=\"5\" height=\"5\"/></svg>"},
            {"圆",    "<svg viewBox=\"0 0 10 10\"><circle cx=\"5\" cy=\"5\" r=\"3\"/></svg>"},
            {"路径",  "<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 L9 0 L9 9 Z\"/></svg>"},
            {"贝塞尔","<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 C0 5 10 5 10 0\"/></svg>"},
            {"不支持","<svg viewBox=\"0 0 10 10\"><text x=\"0\" y=\"0\">x</text></svg>"},
        };
        for (size_t i = 0; i < sizeof(docs)/sizeof(docs[0]); i++) {
            fx_svg_t *s = fx_svg_parse(docs[i].s);
            printf("   %-8s 形状数=%d  跳过=%d\n", docs[i].n,
                   fx_svg_shape_count(s), fx_svg_skipped(s));
            fx_svg_free(s);
        }
    }

    printf("\n========== 证据收集完毕 ==========\n");
    return 0;
}
