/**
 * svg_test.c — v2.5 SVG 子集解析自检 (无头)
 *
 * 【这里验什么】只验【解析结果】: 形状数、点数、包围盒(证明 transform 真的生效)、
 *   不支持元素的跳过计数。渲染路径(fx_svg_draw)依赖框架的绘制调用, 不在本测试里。
 *
 * 【为什么 bbox 是关键断言】transform 写错是 SVG 里最隐蔽的一类 bug ——
 *   图还能画出来, 就是位置/大小不对。只看"形状数对不对"抓不到, 必须看坐标。
 */
#include "fxtk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* fx_svg_load 需要它们; 本测试只用 fx_svg_parse, 所以给个最小桩即可(不链 backends) */
void *fx_file_read(const char *path, int *out_size) { (void)path; if (out_size) *out_size = 0; return NULL; }
void  fx_file_free(void *buf) { (void)buf; }

/* ===================== 绘制调用录制器 =====================
 * fx_svg_draw 最终落到这些框架绘制函数上。与其给空桩, 不如把它们做成【录制器】,
 * 这样"解析对不对"和"画出来落到哪个图元、落在哪、什么颜色"都能一起验。 */
enum { R_FRECT=0, R_FRECTR, R_DRECT, R_DRECTR, R_FCIRC, R_DCIRC,
       R_FELLI, R_DELLI, R_FPOLY, R_DPOLY, R_DLINE };

typedef struct { int kind; int a,b,c,d; fx_color_t col; } rcall_t;
static rcall_t s_rc[512];
static int      s_rcn = 0;
static fx_color_t s_curcol = 0;

static void rec(int k, int a, int b, int c, int d)
{
    if (s_rcn < 512) {
        s_rc[s_rcn].kind = k; s_rc[s_rcn].a = a; s_rc[s_rcn].b = b;
        s_rc[s_rcn].c = c; s_rc[s_rcn].d = d; s_rc[s_rcn].col = s_curcol;
        s_rcn++;
    }
}
static void rec_reset(void) { s_rcn = 0; }
static int  rec_count(int k) { int n=0; for (int i=0;i<s_rcn;i++) if (s_rc[i].kind==k) n++; return n; }
static const rcall_t *rec_find(int k)
{
    for (int i = 0; i < s_rcn; i++) if (s_rc[i].kind == k) return &s_rc[i];
    return NULL;
}

void fx_set_color(fx_color_t c) { s_curcol = c; }
void fx_fill_rect(int x1,int y1,int x2,int y2) { rec(R_FRECT,x1,y1,x2,y2); }
void fx_fill_rect_round(int x1,int y1,int x2,int y2,int r) { (void)r; rec(R_FRECTR,x1,y1,x2,y2); }
void fx_draw_rect(int x1,int y1,int x2,int y2) { rec(R_DRECT,x1,y1,x2,y2); }
void fx_draw_rect_round(int x1,int y1,int x2,int y2,int r) { (void)r; rec(R_DRECTR,x1,y1,x2,y2); }
void fx_fill_circle(int cx,int cy,int r) { rec(R_FCIRC,cx,cy,r,0); }
void fx_draw_circle(int cx,int cy,int r) { rec(R_DCIRC,cx,cy,r,0); }
void fx_fill_ellipse(int cx,int cy,int rx,int ry) { rec(R_FELLI,cx,cy,rx,ry); }
void fx_draw_ellipse(int cx,int cy,int rx,int ry) { rec(R_DELLI,cx,cy,rx,ry); }
void fx_fill_polygon(const int16_t *pts,int n) { rec(R_FPOLY, pts?pts[0]:0, pts?pts[1]:0, n, 0); }
void fx_draw_polygon(const int16_t *pts,int n) { rec(R_DPOLY, pts?pts[0]:0, pts?pts[1]:0, n, 0); }
void fx_draw_line(int x1,int y1,int x2,int y2) { rec(R_DLINE,x1,y1,x2,y2); }

static int s_fail = 0;

static void chk(const char *name, int ok)
{
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) s_fail++;
}

static int nearf(float a, float b) { return fabsf(a - b) < 0.01f; }

static void chk_bbox(const char *name, const fx_svg_t *s, int i,
                     float x1, float y1, float x2, float y2)
{
    float a, b, c, d;
    if (!fx_svg_shape_bbox(s, i, &a, &b, &c, &d)) {
        printf("  [FAIL] %s (取不到第 %d 个形状的包围盒)\n", name, i);
        s_fail++; return;
    }
    int ok = nearf(a,x1) && nearf(b,y1) && nearf(c,x2) && nearf(d,y2);
    printf("  %s %s", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) printf("  实测 (%.1f,%.1f)-(%.1f,%.1f) 期望 (%.1f,%.1f)-(%.1f,%.1f)", a,b,c,d, x1,y1,x2,y2);
    printf("\n");
    if (!ok) s_fail++;
}

int main(void)
{
    printf("== fxtk v2.5 SVG 子集自检 ==\n");

    printf("\n-- ① viewBox / 空文档 --\n");
    {
        fx_svg_t *s = fx_svg_parse("<svg viewBox=\"0 0 24 24\"></svg>");
        float vw = 0, vh = 0;
        chk("解析成功", s != NULL);
        chk("viewBox 24x24 读到", fx_svg_size(s, &vw, &vh) && nearf(vw,24) && nearf(vh,24));
        chk("无形状 → 形状数 0", fx_svg_shape_count(s) == 0);
        fx_svg_free(s);

        s = fx_svg_parse("<svg width=\"32\" height=\"16\"></svg>");
        chk("无 viewBox 时退回 width/height", fx_svg_size(s, &vw, &vh) && nearf(vw,32) && nearf(vh,16));
        fx_svg_free(s);

        s = fx_svg_parse("<svg></svg>");
        chk("都没有时退化为 100x100(不是 0, 避免除零)", fx_svg_size(s, &vw, &vh) && vw > 0 && vh > 0);
        fx_svg_free(s);

        chk("NULL 输入返回 NULL", fx_svg_parse(NULL) == NULL);
    }

    printf("\n-- ② 各元素的形状数与包围盒 --\n");
    {
        const char *doc =
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\"/>"
            "  <circle cx=\"50\" cy=\"50\" r=\"10\"/>"
            "  <ellipse cx=\"20\" cy=\"80\" rx=\"8\" ry=\"4\"/>"
            "  <line x1=\"0\" y1=\"0\" x2=\"100\" y2=\"100\"/>"
            "  <polyline points=\"0,0 10,10 20,0\"/>"
            "  <polygon points=\"0,0 10,0 10,10\"/>"
            "</svg>";
        fx_svg_t *s = fx_svg_parse(doc);
        chk("6 个元素 → 6 个形状", fx_svg_shape_count(s) == 6);
        chk_bbox("rect (10,20)-(40,60)",        s, 0, 10,20,40,60);
        chk_bbox("circle cx50 cy50 r10",        s, 1, 40,40,60,60);
        chk_bbox("ellipse cx20 cy80 rx8 ry4",   s, 2, 12,76,28,84);
        chk_bbox("line (0,0)-(100,100)",        s, 3, 0,0,100,100);
        chk_bbox("polyline 3 点",               s, 4, 0,0,20,10);
        chk_bbox("polygon 3 点",                s, 5, 0,0,10,10);
        chk("polyline 3 个点", fx_svg_shape_points(s,4) == 3);
        chk("polygon 3 个点",  fx_svg_shape_points(s,5) == 3);
        chk("没有跳过任何元素", fx_svg_skipped(s) == 0);
        fx_svg_free(s);
    }

    printf("\n-- ③ transform 真的生效了吗 (最隐蔽的一类 bug) --\n");
    {
        fx_svg_t *s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\" transform=\"translate(5,7)\"/>"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\" transform=\"scale(2)\"/>"
            "  <circle cx=\"50\" cy=\"50\" r=\"10\" transform=\"translate(10,0) scale(2)\"/>"
            "</svg>");
        chk("3 个形状", fx_svg_shape_count(s) == 3);
        chk_bbox("translate(5,7) → (15,27)-(45,67)", s, 0, 15,27,45,67);
        chk_bbox("scale(2) → (20,40)-(80,120)",      s, 1, 20,40,80,120);
        /* ⚠️ SVG 语义: transform="A B" 是【先 B 后 A】作用到点上(矩阵 A·B·p)。
         *    所以这里圆心 50 → scale(2) 得 100 → translate(10,0) 得 110, 半径 10→20。
         *    初版测试按"(50+10)*2=120"算, 是把顺序搞反了, 实测 110 才是对的。 */
        chk_bbox("translate(10,0)+scale(2) → 圆心 110, 半径 20", s, 2, 90,80,130,120);
        fx_svg_free(s);

        /* rotate(90) 绕原点: 矩形 (10,20)-(40,60) → x'=-y, y'=x → (-60,10)-(-20,40) */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\" transform=\"rotate(90)\"/>"
            "</svg>");
        chk_bbox("rotate(90) 后包围盒转了 90 度", s, 0, -60,10,-20,40);
        fx_svg_free(s);

        /* <g> 上的 transform 要被子元素继承 */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <g transform=\"translate(100,0)\">"
            "    <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\"/>"
            "    <rect x=\"0\" y=\"0\" width=\"5\" height=\"5\"/>"
            "  </g>"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\"/>"
            "</svg>");
        chk("3 个形状(g 内 2 + 外 1)", fx_svg_shape_count(s) == 3);
        chk_bbox("<g> 的 translate 被子元素继承 (110,20)-(140,60)", s, 0, 110,20,140,60);
        chk_bbox("<g> 内第二个也继承 (100,0)-(105,5)",              s, 1, 100,0,105,5);
        chk_bbox("<g> 结束后恢复 (10,20)-(40,60)",                  s, 2, 10,20,40,60);
        fx_svg_free(s);
    }

    printf("\n-- ④ 路径 d 展平 --\n");
    {
        fx_svg_t *s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <path d=\"M10 10 L90 10 L90 90 Z\"/>"
            "</svg>");
        chk("直线路径 3 点(闭合后去掉重复尾点)", fx_svg_shape_points(s, 0) == 3);
        chk_bbox("三角形包围盒", s, 0, 10,10,90,90);
        fx_svg_free(s);

        /* C 曲线固定 12 段细分 → 起点 1 + 12 = 13 点 */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <path d=\"M0 0 C0 50 100 50 100 0\"/>"
            "</svg>");
        chk("三次贝塞尔展平成 13 点(起点+12 段)", fx_svg_shape_points(s, 0) == 13);
        fx_svg_free(s);

        /* 相对命令: m/l 与绝对等价 */
        fx_svg_t *a = fx_svg_parse("<svg viewBox=\"0 0 100 100\"><path d=\"M10 10 L90 10 L90 90\"/></svg>");
        fx_svg_t *b = fx_svg_parse("<svg viewBox=\"0 0 100 100\"><path d=\"m10 10 l80 0 l0 80\"/></svg>");
        float a1,b1,c1,d1,a2,b2,c2,d2;
        fx_svg_shape_bbox(a,0,&a1,&b1,&c1,&d1);
        fx_svg_shape_bbox(b,0,&a2,&b2,&c2,&d2);
        chk("相对命令 m/l 与绝对 M/L 结果一致",
            nearf(a1,a2)&&nearf(b1,b2)&&nearf(c1,c2)&&nearf(d1,d2));
        fx_svg_free(a); fx_svg_free(b);

        s = fx_svg_parse("<svg viewBox=\"0 0 100 100\"><path d=\"M0 0 H50 V50\"/></svg>");
        chk_bbox("H/V 水平垂直线", s, 0, 0,0,50,50);
        fx_svg_free(s);

        s = fx_svg_parse("<svg viewBox=\"0 0 100 100\"><path d=\"M0 0 Q50 100 100 0\"/></svg>");
        chk("二次贝塞尔也展平", fx_svg_shape_points(s,0) == 13);
        fx_svg_free(s);
    }

    printf("\n-- ⑤ 不支持的输入: 跳过并计数, 不崩不假装画了 --\n");
    {
        fx_svg_t *s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <text x=\"0\" y=\"0\">hi</text>"
            "  <defs><linearGradient id=\"g\"/></defs>"
            "  <rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
            "  <use href=\"#g\"/>"
            "</svg>");
        chk("只解析出 1 个形状(rect)", fx_svg_shape_count(s) == 1);
        chk("跳过了 4 个不认识的元素(text/defs/linearGradient/use)",
            fx_svg_skipped(s) == 4);
        fx_svg_free(s);

        /* 圆弧 A 未支持: 不应崩, 且要让调用方知道被简化了 */
        s = fx_svg_parse("<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 A5 5 0 0 1 10 10\"/></svg>");
        chk("A 圆弧指令: 不崩, 且计入 skipped", s != NULL && fx_svg_skipped(s) >= 1);
        fx_svg_free(s);

        s = fx_svg_parse("<svg viewBox=\"0 0 10 10\"><path d=\"garbage\"/></svg>");
        chk("路径 d 内容非法: 不崩", s != NULL);
        fx_svg_free(s);

        chk("空字符串不崩", fx_svg_parse("") != NULL);
        chk("不是 XML 也不崩", fx_svg_parse("hello world") != NULL);
        fx_svg_free(fx_svg_parse("hello world"));
    }

    printf("\n-- ⑥ 退化输入与越界防御 --\n");
    {
        fx_svg_t *s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"0\" y=\"0\" width=\"0\" height=\"10\"/>"      /* 宽 0: 应丢弃 */
            "  <circle cx=\"5\" cy=\"5\" r=\"0\"/>"                    /* 半径 0: 应丢弃 */
            "  <rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" rx=\"3\"/>"
            "</svg>");
        chk("宽/半径为 0 的形状被丢弃(只留圆角矩形)", fx_svg_shape_count(s) == 1);
        chk("越界索引返回 -1", fx_svg_shape_points(s, 99) == -1);
        chk("负索引返回 -1",   fx_svg_shape_points(s, -1) == -1);
        chk("越界 bbox 返回 0", fx_svg_shape_bbox(s, 99, NULL, NULL, NULL, NULL) == 0);
        fx_svg_free(s);

        chk("shape_points(NULL) 返回 -1", fx_svg_shape_points(NULL, 0) == -1);
        chk("shape_count(NULL) 返回 0",   fx_svg_shape_count(NULL) == 0);
        fx_svg_free(NULL);   /* 不应崩 */
        chk("fx_svg_free(NULL) 安全", 1);
    }

    printf("\n-- ⑦ 颜色解析(经 bbox 间接口径验不到, 这里只看不崩) --\n");
    {
        const char *cols[] = { "#f00", "#ff0000", "rgb(1,2,3)", "rgb(1, 2, 3)",
                               "red", "none", "transparent", "#gggggg", "nosuchcolor" };
        int okall = 1;
        for (size_t i = 0; i < sizeof(cols)/sizeof(cols[0]); i++) {
            char doc[256];
            snprintf(doc, sizeof doc,
                     "<svg viewBox=\"0 0 10 10\"><rect x=\"0\" y=\"0\" width=\"5\" height=\"5\" "
                     "fill=\"%s\" stroke=\"%s\"/></svg>", cols[i], cols[i]);
            fx_svg_t *s = fx_svg_parse(doc);
            if (!s || fx_svg_shape_count(s) != 1) okall = 0;
            fx_svg_free(s);
        }
        chk("9 种颜色写法(含非法值)都不崩且形状仍在", okall);
    }

    printf("\n-- ⑧ 渲染路径: fx_svg_draw 落到哪个图元/落在哪/什么颜色 --\n");
    {
        /* viewBox 100x100 → 目标框 200x200: 缩放 2, 无偏移 */
        fx_svg_t *s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"10\" y=\"20\" width=\"30\" height=\"40\" fill=\"#ff0000\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 200, 200);
        const rcall_t *r = rec_find(R_FRECT);
        chk("矩形走原生 fill_rect(不是多边形采样)", r != NULL && rec_count(R_FPOLY) == 0);
        /* SVG width=30 (半开 [10,40)) 在 2 倍下宽 60px; 闭区间端点为 (20,40)-(79,119) */
        chk("坐标按 2 倍缩放且端点换算正确: (20,40)-(79,119)",
            r && r->a == 20 && r->b == 40 && r->c == 79 && r->d == 119);
        chk("宽度恰好 60px(没多画也没少画)", r && (r->c - r->a + 1) == 60);
        chk("颜色 #ff0000 正确传到绘制层", r && (r->col & 0xFFFFFF) == 0xFF0000);
        fx_svg_free(s);

        /* 非正方目标框: 必须【等比缩放+居中】, 不能拉扁 */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"0\" y=\"0\" width=\"50\" height=\"50\" fill=\"#0000ff\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 400, 200);   /* sc = min(4,2) = 2; 水平居中偏移 100 */
        r = rec_find(R_FRECT);
        chk("等比缩放(取较小比例 2, 不拉伸)", r && (r->c - r->a + 1) == 100);
        chk("水平居中: 偏移 (400-200)/2 = 100", r && r->a == 100 && r->c == 199);
        chk("垂直无偏移", r && r->b == 0);
        fx_svg_free(s);

        /* 圆走原生 fill_circle */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <circle cx=\"50\" cy=\"50\" r=\"10\" fill=\"#00ff00\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 100, 100);   /* sc = 1 */
        r = rec_find(R_FCIRC);
        chk("圆走原生 fill_circle", r != NULL && rec_count(R_FPOLY) == 0);
        chk("圆心与半径对: (50,50) r=10", r && r->a == 50 && r->b == 50 && r->c == 10);
        fx_svg_free(s);

        /* stroke-only 的 line: 应产生 draw_line, 不应产生填充 */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <line x1=\"0\" y1=\"0\" x2=\"50\" y2=\"50\" stroke=\"#000000\" stroke-width=\"2\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 100, 100);
        chk("line → draw_line 一次", rec_count(R_DLINE) == 1);
        chk("line 不产生任何填充", rec_count(R_FRECT)==0 && rec_count(R_FPOLY)==0 && rec_count(R_FCIRC)==0);
        fx_svg_free(s);

        /* 多边形填充 */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 10 10\">"
            "  <polygon points=\"0,0 10,0 5,10\" fill=\"#123456\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 10, 10);
        chk("闭合多边形 → fill_polygon", rec_count(R_FPOLY) == 1);
        chk("多边形点数 3 传到绘制层", rec_find(R_FPOLY) && rec_find(R_FPOLY)->c == 3);
        fx_svg_free(s);

        /* 旋转过的矩形应退化为多边形(轴对齐原生图元画不了旋转矩形) */
        s = fx_svg_parse(
            "<svg viewBox=\"0 0 100 100\">"
            "  <rect x=\"10\" y=\"10\" width=\"20\" height=\"20\" fill=\"#ffffff\" transform=\"rotate(30)\"/>"
            "</svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 100, 100);
        chk("旋转矩形退化为多边形填充(而非错误的轴对齐矩形)",
            rec_count(R_FPOLY) == 1 && rec_count(R_FRECT) == 0);
        fx_svg_free(s);

        /* 空文档: 一次绘制调用都不应发出 */
        s = fx_svg_parse("<svg viewBox=\"0 0 10 10\"></svg>");
        rec_reset();
        fx_svg_draw(s, 0, 0, 50, 50);
        chk("空 SVG 不发任何绘制调用", s_rcn == 0);
        fx_svg_free(s);

        /* 退化入参 */
        rec_reset();
        fx_svg_draw(NULL, 0, 0, 10, 10);
        fx_svg_draw(s, 0, 0, 0, 0);
        chk("NULL / 零尺寸不崩也不画", s_rcn == 0);
    }

    printf("\n== 结果: %s (失败 %d 项) ==\n", s_fail ? "FAIL" : "PASS", s_fail);
    printf("注: 渲染测试用【录制器替身】验调用序列与坐标, 真像素效果由金图/演示覆盖。\n");
    return s_fail ? 1 : 0;
}
