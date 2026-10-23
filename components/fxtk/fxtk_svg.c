/**
 * fxtk_svg.c — v2.5: SVG 子集 → fxtk_canvas
 *
 * 【怎么用】
 *   解析一次(init 里), 每帧在 canvas 回调里画(会按目标框缩放):
 *       static fx_svg_t *g_icon;
 *       static void on_canvas(fx_widget_t *w, void *ud) {
 *           int cw, ch; fx_canvas_size(w, &cw, &ch);
 *           fx_canvas_clear(w, FX_WHITE);
 *           fx_svg_draw(g_icon, 0, 0, cw, ch);      // 自动按 viewBox 等比缩放+居中
 *       }
 *       g_icon = fx_svg_load("icon.svg");           // 或 fx_svg_parse(text)
 *   析构时 fx_svg_free(g_icon)。
 *
 * 【为什么解析与绘制分开】
 *   SVG 文本解析要做很多字符串/几何运算, 每帧做一次纯属浪费; 而且拆开后
 *   【解析结果可以单测】(形状数、坐标、颜色), 【绘制过程可以用假驱动单测】
 *   (记录 draw 调用序列) —— 不需要真窗口。见 demo-main/test/svg_test.c。
 *
 * 【支持的子集(明确列出, 不假装全支持)】
 *   元素: <svg> <g> <rect> <circle> <ellipse> <line> <polyline> <polygon> <path>
 *   属性: viewBox/width/height, x/y/rx/ry/cx/cy/r/x1/y1/x2/y2/points/d,
 *         fill/stroke/stroke-width/transform(translate,scale,rotate,matrix)
 *   路径: M m L l H h V v C c Q q Z z
 *   颜色: #rgb #rrggbb rgb(r,g,b) none transparent + 常用颜色名
 *
 * 【不支持(不装做支持)】
 *   ✗ 圆弧指令 A/a 与平滑曲线 S/s T/t  (需要贝塞尔/椭圆弧求交, 本版没做)
 *   ✗ 渐变 <linearGradient>/<radialGradient>、图案、滤镜
 *   ✗ <text>/<tspan> (文字要走框架自己的字体管线, 与本模块的几何模型不同)
 *   ✗ CSS style 属性与 <style> 块 (只认表示属性)
 *   ✗ <use>/<defs>/<symbol>/裁剪与蒙版、opacity 混合
 *   遇到不认识的元素/属性: 【跳过并计数】, 不报错也不假装画了 —— 用
 *   fx_svg_skipped() 取"跳过了多少", 便于调用方知道自己的图有没有被简化。
 */
#include "fxtk.h"
#include "fxtk_backends.h"   /* fx_file_read / fx_file_free */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ===================== 形状与文档模型 ===================== */

typedef enum {
    SV_SH_RECT = 0, SV_SH_ROUNDRECT, SV_SH_CIRCLE, SV_SH_ELLIPSE, SV_SH_POLY
} sv_kind_t;

#define SV_MAX_PTS 4096   /* 单个形状最多这么多点(路径曲线展平后可能很多) */

typedef struct {
    sv_kind_t kind;
    int   closed;          /* POLY: 1=闭合(可填充), 0=折线 */
    int   n;               /* 点数 */
    int   cap;
    float *pts;            /* x0,y0,x1,y1,... */
    /* 原生图元参数(未旋转时用, 画质比多边形采样好) */
    int   native;          /* 1 = 可直接用原生图元画 */
    float x1, y1, x2, y2;  /* RECT/ROUNDRECT */
    float rx, ry;          /* ROUNDRECT 圆角 */
    float cx, cy, rr;      /* CIRCLE */
    /* 样式 */
    int   has_fill, has_stroke;
    fx_color_t fill, stroke;
    float stroke_w;
} sv_shape_t;

struct fx_svg {
    float vw, vh;          /* viewBox (无则用 width/height, 再退化为 100x100) */
    sv_shape_t *sh;
    int n, cap;
    int skipped;           /* 被跳过的元素/指令计数 */
};

/* ===================== 仿射变换 ===================== */

typedef struct { float a,b,c,d,e,f; } sv_aff_t;

static sv_aff_t aff_id(void) { sv_aff_t m = {1,0,0,1,0,0}; return m; }

static sv_aff_t aff_mul(sv_aff_t m, sv_aff_t n)
{
    sv_aff_t r;
    r.a = m.a*n.a + m.c*n.b;   r.b = m.b*n.a + m.d*n.b;
    r.c = m.a*n.c + m.c*n.d;   r.d = m.b*n.c + m.d*n.d;
    r.e = m.a*n.e + m.c*n.f + m.e;
    r.f = m.b*n.e + m.d*n.f + m.f;
    return r;
}

static void aff_pt(sv_aff_t m, float x, float y, float *ox, float *oy)
{
    *ox = m.a*x + m.c*y + m.e;
    *oy = m.b*x + m.d*y + m.f;
}

/* 只有平移/缩放(无旋转/斜切)时, 轴对齐图元仍可用原生调用 */
static int aff_axis_aligned(sv_aff_t m) { return m.b == 0.0f && m.c == 0.0f; }

/* ===================== 颜色 ===================== */

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static fx_color_t sv_color(const char *s, int *ok)
{
    *ok = 1;
    if (!s) { *ok = 0; return 0; }
    while (*s == ' ' || *s == '\t') s++;
    if (!strcmp(s, "none") || !strcmp(s, "transparent")) { *ok = 0; return 0; }

    if (s[0] == '#') {
        int h[6], n = 0;
        for (const char *p = s + 1; *p && n < 6; p++) {
            int v = hexv(*p);
            if (v < 0) break;
            h[n++] = v;
        }
        if (n == 3) return FX_RGB(h[0]*17, h[1]*17, h[2]*17);
        if (n == 6) return FX_RGB(h[0]*16+h[1], h[2]*16+h[3], h[4]*16+h[5]);
        *ok = 0; return 0;
    }
    if (!strncmp(s, "rgb(", 4)) {
        int r=0,g=0,b=0;
        if (sscanf(s + 4, "%d , %d , %d", &r, &g, &b) == 3 ||
            sscanf(s + 4, "%d,%d,%d", &r, &g, &b) == 3) {
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            return FX_RGB(r,g,b);
        }
        *ok = 0; return 0;
    }
    /* 常用颜色名 (够画图标用; 不做完整的 147 个 CSS 颜色) */
    struct { const char *n; fx_color_t c; } tab[] = {
        {"black",0x000000},{"white",0xFFFFFF},{"red",0xFF0000},{"green",0x008000},
        {"blue",0x0000FF},{"yellow",0xFFFF00},{"gray",0x808080},{"grey",0x808080},
        {"silver",0xC0C0C0},{"orange",0xFFA500},{"purple",0x800080},{"cyan",0x00FFFF},
        {"magenta",0xFF00FF},{"lime",0x00FF00},{"navy",0x000080},{"teal",0x008080},
        {"maroon",0x800000},{"olive",0x808000},{"pink",0xFFC0CB},{"brown",0xA52A2A},
    };
    for (size_t i = 0; i < sizeof(tab)/sizeof(tab[0]); i++)
        if (!strcmp(s, tab[i].n)) return tab[i].c;
    *ok = 0; return 0;
}

/* ===================== 形状容器 ===================== */

static sv_shape_t *sv_new_shape(struct fx_svg *d)
{
    if (d->n >= d->cap) {
        int nc = d->cap ? d->cap * 2 : 16;
        sv_shape_t *ns = (sv_shape_t *)realloc(d->sh, (size_t)nc * sizeof(sv_shape_t));
        if (!ns) return NULL;
        d->sh = ns; d->cap = nc;
    }
    sv_shape_t *s = &d->sh[d->n++];
    memset(s, 0, sizeof *s);
    s->stroke_w = 1.0f;
    return s;
}

static int sv_push_pt(sv_shape_t *s, float x, float y)
{
    if (s->n >= SV_MAX_PTS) return 0;
    if (s->n >= s->cap) {
        int nc = s->cap ? s->cap * 2 : 16;
        if (nc > SV_MAX_PTS) nc = SV_MAX_PTS;
        float *np = (float *)realloc(s->pts, (size_t)nc * 2 * sizeof(float));
        if (!np) return 0;
        s->pts = np; s->cap = nc;
    }
    s->pts[s->n*2+0] = x; s->pts[s->n*2+1] = y;
    s->n++;
    return 1;
}

/* ===================== 属性解析小工具 ===================== */

/* 在 attr 串里找 key="value" 或 key='value', 返回 value 的起止(不含引号); 找不到返回 0 */
/* v2.5.1(M1): 属性搜索必须限制在【本元素】的 <...> 区间 [tag,end) 内。
 * 旧实现只拿到元素起始指针、用 strstr 搜到整个文档末尾 → 前一个元素缺属性时会命中
 * 后续元素的同名属性(最小例: rect 缺 x/y 继承第二个 rect 的 x=40,y=40)。 */
static int sv_attr(const char *tag, const char *end, const char *key, const char **vs, int *vlen)
{
    size_t kl = strlen(key);
    const char *p = tag;
    while (p + kl <= end) {
        const char *hit = NULL;
        for (const char *q = p; q + kl <= end; q++)
            if (memcmp(q, key, kl) == 0) { hit = q; break; }
        if (!hit) break;
        p = hit;
        /* key 必须是独立的名字(前面是空白/<, 后面紧跟 = ) */
        int okhead = (p == tag) || isspace((unsigned char)p[-1]) || p[-1] == '<';
        if (okhead && p + kl < end && p[kl] == '=') {
            const char *q = p + kl + 1;
            while (q < end && (*q == ' ' || *q == '\t')) q++;
            if (q >= end) return 0;
            if (*q == '"' || *q == '\'') {
                char qc = *q; q++;
                const char *e = q;
                while (e < end && *e != qc) e++;
                if (e >= end) return 0;
                *vs = q; *vlen = (int)(e - q);
                return 1;
            }
            const char *e = q;
            while (e < end && *e && !isspace((unsigned char)*e) && *e != '>') e++;
            *vs = q; *vlen = (int)(e - q);
            return 1;
        }
        p += kl;
    }
    return 0;
}

static float sv_num(const char *tag, const char *end, const char *key, float dflt)
{
    const char *v; int l;
    if (!sv_attr(tag, end, key, &v, &l)) return dflt;
    char buf[64]; if (l > 63) l = 63;
    memcpy(buf, v, (size_t)l); buf[l] = 0;
    return (float)atof(buf);
}

/* 取绝对/相对属性: SVG 里 fill/stroke 会从父级继承, 这里用"未设置=-1"表示继承 */
static int sv_attr_str(const char *tag, const char *end, const char *key, char *out, int outsz)
{
    const char *v; int l;
    if (!sv_attr(tag, end, key, &v, &l)) return 0;
    if (l > outsz - 1) l = outsz - 1;
    memcpy(out, v, (size_t)l); out[l] = 0;
    return 1;
}

/* 解析 "1,2 3,4" / "1 2 3 4" 形式的数列表 */
static int sv_numlist(const char *s, int len, float *out, int maxn)
{
    int n = 0;
    const char *p = s, *end = s + len;
    while (p < end && n < maxn) {
        while (p < end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= end) break;
        char *e;
        float v = strtof(p, &e);
        if (e == p) break;
        out[n++] = v;
        p = e;
    }
    return n;
}

/* 解析 transform="translate(..) scale(..) rotate(..) matrix(..)" */
static sv_aff_t sv_transform(const char *tag, const char *end, sv_aff_t base)
{
    char buf[256];
    if (!sv_attr_str(tag, end, "transform", buf, sizeof buf)) return base;
    sv_aff_t m = base;
    const char *p = buf;
    while (*p) {
        while (*p && !isalpha((unsigned char)*p)) p++;
        if (!*p) break;
        char fn[16]; int fl = 0;
        while (*p && isalpha((unsigned char)*p) && fl < 15) fn[fl++] = *p++;
        fn[fl] = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '(') continue;
        p++;
        const char *e = strchr(p, ')');
        if (!e) break;
        float a[6]; int n = sv_numlist(p, (int)(e - p), a, 6);
        sv_aff_t t = aff_id();
        if (!strcmp(fn, "translate") && n >= 1) { t.e = a[0]; t.f = (n >= 2) ? a[1] : 0; }
        else if (!strcmp(fn, "scale") && n >= 1) { t.a = a[0]; t.d = (n >= 2) ? a[1] : a[0]; }
        else if (!strcmp(fn, "rotate") && n >= 1) {
            float r = a[0] * 3.14159265358979f / 180.0f;
            float cs = cosf(r), sn = sinf(r);
            sv_aff_t rot = { cs, sn, -sn, cs, 0, 0 };
            if (n >= 3) {
                sv_aff_t tp = {1,0,0,1,a[1],a[2]}, tn = {1,0,0,1,-a[1],-a[2]};
                t = aff_mul(tp, aff_mul(rot, tn));
            } else t = rot;
        } else if (!strcmp(fn, "matrix") && n >= 6) { t.a=a[0];t.b=a[1];t.c=a[2];t.d=a[3];t.e=a[4];t.f=a[5]; }
        m = aff_mul(m, t);
        p = e + 1;
    }
    return m;
}

/* ===================== 路径展平 ===================== */

/* 把 d="..." 展平成折线。曲线用固定分段数采样(画布是像素级, 细分再多也看不出)
 * —— 这是【近似】, 不是精确贝塞尔。分段数按段长自适应会更好, 但先把可用的做出来。 */
static void sv_path(struct fx_svg *d, const char *dstr, int dlen,
                    sv_aff_t m, int has_fill, fx_color_t fill, int has_stroke,
                    fx_color_t stroke, float sw)
{
    sv_shape_t *s = sv_new_shape(d);
    if (!s) return;
    s->kind = SV_SH_POLY; s->closed = 0;
    s->has_fill = has_fill; s->fill = fill;
    s->has_stroke = has_stroke; s->stroke = stroke; s->stroke_w = sw;

    float cx = 0, cy = 0;       /* 当前点(用户坐标) */
    float sx = 0, sy = 0;       /* 子路径起点 */
    int have_start = 0;
    const char *p = dstr, *end = dstr + dlen;

    while (p < end) {
        while (p < end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= end) break;
        char cmd = *p;
        if (!isalpha((unsigned char)cmd)) { d->skipped++; break; }
        p++;
        int rel = islower((unsigned char)cmd);
        char C = (char)toupper((unsigned char)cmd);

        if (C == 'Z') {
            if (have_start) {
                float ox, oy; aff_pt(m, sx, sy, &ox, &oy);
                sv_push_pt(s, ox, oy);
                cx = sx; cy = sy;
            }
            continue;
        }
        /* 取本段的参数: 按命令需要的个数取, 且允许后续隐式重复 */
        int want = (C=='H'||C=='V') ? 1 : (C=='M'||C=='L') ? 2 : (C=='C') ? 6 : (C=='Q') ? 4 : 0;
        if (!want) { d->skipped++; break; }
        float a[6];
        int got = sv_numlist(p, (int)(end - p), a, want);
        if (got < want) break;
        /* 推进 p 到本段参数之后 */
        {   int cnt = 0; const char *q = p;
            while (q < end && cnt < want) {
                while (q < end && (isspace((unsigned char)*q) || *q == ',')) q++;
                if (q >= end) break;
                char *e2; strtof(q, &e2); if (e2 == q) break; q = e2; cnt++;
            }
            p = q;
        }

        if (C == 'M') {
            cx = rel ? cx + a[0] : a[0];
            cy = rel ? cy + a[1] : a[1];
            sx = cx; sy = cy; have_start = 1;
            float ox, oy; aff_pt(m, cx, cy, &ox, &oy);
            sv_push_pt(s, ox, oy);
        } else if (C == 'L') {
            cx = rel ? cx + a[0] : a[0];
            cy = rel ? cy + a[1] : a[1];
            float ox, oy; aff_pt(m, cx, cy, &ox, &oy);
            sv_push_pt(s, ox, oy);
        } else if (C == 'H') {
            cx = rel ? cx + a[0] : a[0];
            float ox, oy; aff_pt(m, cx, cy, &ox, &oy);
            sv_push_pt(s, ox, oy);
        } else if (C == 'V') {
            cy = rel ? cy + a[0] : a[0];
            float ox, oy; aff_pt(m, cx, cy, &ox, &oy);
            sv_push_pt(s, ox, oy);
        } else if (C == 'C' || C == 'Q') {
            float x0 = cx, y0 = cy;
            float p1x, p1y, p2x, p2y, ex, ey;
            if (C == 'C') {
                p1x = rel ? x0+a[0] : a[0]; p1y = rel ? y0+a[1] : a[1];
                p2x = rel ? x0+a[2] : a[2]; p2y = rel ? y0+a[3] : a[3];
                ex  = rel ? x0+a[4] : a[4]; ey  = rel ? y0+a[5] : a[5];
            } else {
                p1x = rel ? x0+a[0] : a[0]; p1y = rel ? y0+a[1] : a[1];
                p2x = p1x; p2y = p1y;
                ex  = rel ? x0+a[2] : a[2]; ey  = rel ? y0+a[3] : a[3];
            }
            const int SEG = 12;   /* 固定细分: 像素级足够, 且行为可预期(便于单测) */
            for (int i = 1; i <= SEG; i++) {
                float t = (float)i / SEG, u = 1.0f - t;
                float bx, by;
                if (C == 'C') {
                    bx = u*u*u*x0 + 3*u*u*t*p1x + 3*u*t*t*p2x + t*t*t*ex;
                    by = u*u*u*y0 + 3*u*u*t*p1y + 3*u*t*t*p2y + t*t*t*ey;
                } else {
                    bx = u*u*x0 + 2*u*t*p1x + t*t*ex;
                    by = u*u*y0 + 2*u*t*p1y + t*t*ey;
                }
                float ox, oy; aff_pt(m, bx, by, &ox, &oy);
                sv_push_pt(s, ox, oy);
            }
            cx = ex; cy = ey;
        }
    }
    /* 首尾重合(或有 Z)视作闭合, 这样填充才成立 */
    if (s->n >= 3) {
        float dx = s->pts[0] - s->pts[(s->n-1)*2];
        float dy = s->pts[1] - s->pts[(s->n-1)*2+1];
        if (dx*dx + dy*dy < 0.01f) { s->closed = 1; s->n--; }
    }
    if (s->n < 2) { s->n = 0; }
}

/* ===================== 解析 ===================== */

/* 样式继承: 未设置时沿用父级 */
typedef struct {
    int has_fill; fx_color_t fill;
    int has_stroke; fx_color_t stroke;
    float stroke_w;
} sv_style_t;

static void sv_style_from_tag(const char *tag, const char *end, sv_style_t *st)
{
    char v[64];
    if (sv_attr_str(tag, end, "fill", v, sizeof v)) {
        int ok; fx_color_t c = sv_color(v, &ok);
        st->has_fill = ok; st->fill = c;
    }
    if (sv_attr_str(tag, end, "stroke", v, sizeof v)) {
        int ok; fx_color_t c = sv_color(v, &ok);
        st->has_stroke = ok; st->stroke = c;
    }
    {
        const char *pv; int pl;
        if (sv_attr(tag, end, "stroke-width", &pv, &pl)) {
            char b[32]; if (pl > 31) pl = 31;
            memcpy(b, pv, (size_t)pl); b[pl] = 0;
            float f = (float)atof(b);
            if (f > 0) st->stroke_w = f;
        }
    }
}

fx_svg_t *fx_svg_parse(const char *text)
{
    if (!text) return NULL;
    struct fx_svg *d = (struct fx_svg *)calloc(1, sizeof(struct fx_svg));
    if (!d) return NULL;
    d->vw = 0; d->vh = 0;

    sv_style_t st; memset(&st, 0, sizeof st); st.stroke_w = 1.0f;
    sv_aff_t m = aff_id();

    /* 变换栈(只跟 <g> 有关; SVG 子集里够用) */
    sv_aff_t stack[16]; sv_style_t sstack[16]; int depth = 0;

    const char *p = text;
    while ((p = strchr(p, '<'))) {
        if (p[1] == '!' || p[1] == '?') { const char *e = strchr(p, '>'); if (!e) break; p = e + 1; continue; }
        if (p[1] == '/') {                       /* 结束标签 */
            const char *e = strchr(p, '>');
            if (!e) break;
            char nm[16]; int k = 0;
            for (const char *q = p + 2; q < e && k < 15 && !isspace((unsigned char)*q); q++) nm[k++] = *q;
            nm[k] = 0;
            if (!strcmp(nm, "g") && depth > 0) { depth--; m = stack[depth]; st = sstack[depth]; }
            p = e + 1; continue;
        }
        const char *e = strchr(p, '>');
        if (!e) break;
        char nm[16]; int k = 0;
        for (const char *q = p + 1; q < e && k < 15 && !isspace((unsigned char)*q) && *q != '/'; q++) nm[k++] = *q;
        nm[k] = 0;

        if (!strcmp(nm, "svg")) {
            char vb[128];
            if (sv_attr_str(p, e, "viewBox", vb, sizeof vb)) {
                float a[4];
                if (sv_numlist(vb, (int)strlen(vb), a, 4) == 4 && a[2] > 0 && a[3] > 0) { d->vw = a[2]; d->vh = a[3]; }
            }
            if (d->vw <= 0) { d->vw = sv_num(p, e, "width", 100); d->vh = sv_num(p, e, "height", 100); }
        } else if (!strcmp(nm, "g")) {
            if (depth < 16) { stack[depth] = m; sstack[depth] = st; depth++; }
            sv_style_from_tag(p, e, &st);
            m = sv_transform(p, e, m);
        } else if (!strcmp(nm, "rect") || !strcmp(nm, "circle") || !strcmp(nm, "ellipse") ||
                   !strcmp(nm, "line") || !strcmp(nm, "polyline") || !strcmp(nm, "polygon") ||
                   !strcmp(nm, "path")) {
            sv_style_t lst = st;
            sv_style_from_tag(p, e, &lst);
            sv_aff_t lm = sv_transform(p, e, m);

            if (!strcmp(nm, "rect")) {
                float x = sv_num(p, e,"x",0), y = sv_num(p, e,"y",0);
                float w = sv_num(p, e,"width",0), h = sv_num(p, e,"height",0);
                float rx = sv_num(p, e,"rx",-1), ry = sv_num(p, e,"ry",-1);
                if (w > 0 && h > 0) {
                    sv_shape_t *s = sv_new_shape(d);
                    if (s) {
                        s->has_fill=lst.has_fill; s->fill=lst.fill;
                        s->has_stroke=lst.has_stroke; s->stroke=lst.stroke; s->stroke_w=lst.stroke_w;
                        if (aff_axis_aligned(lm)) {          /* 无旋转: 用原生矩形(画质更好) */
                            float ax1,ay1,ax2,ay2;
                            aff_pt(lm, x, y, &ax1, &ay1);
                            aff_pt(lm, x+w, y+h, &ax2, &ay2);
                            if (ax1>ax2){float t=ax1;ax1=ax2;ax2=t;}
                            if (ay1>ay2){float t=ay1;ay1=ay2;ay2=t;}
                            float sc = fabsf(lm.a);
                            if (rx < 0) rx = ry;
                            if (ry < 0) ry = rx;
                            if (rx > 0 || ry > 0) {
                                s->kind = SV_SH_ROUNDRECT;
                                s->rx = ry = (rx > 0 ? rx : ry) * sc;
                                s->ry = s->rx;
                            } else s->kind = SV_SH_RECT;
                            s->native = 1;
                            s->x1=ax1; s->y1=ay1; s->x2=ax2; s->y2=ay2;
                        } else {                              /* 有旋转: 展平成多边形 */
                            s->kind = SV_SH_POLY; s->closed = 1;
                            float c[4][2] = {{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
                            for (int i=0;i<4;i++){ float ox,oy; aff_pt(lm,c[i][0],c[i][1],&ox,&oy); sv_push_pt(s,ox,oy); }
                        }
                    }
                }
            } else if (!strcmp(nm, "circle") || !strcmp(nm, "ellipse")) {
                float cx = sv_num(p, e,"cx",0), cy = sv_num(p, e,"cy",0);
                float rr = sv_num(p, e,"r",-1);
                float rx = (rr >= 0) ? rr : sv_num(p, e,"rx",0);
                float ry = (rr >= 0) ? rr : sv_num(p, e,"ry",0);
                if (rx > 0 || ry > 0) {
                    sv_shape_t *s = sv_new_shape(d);
                    if (s) {
                        s->has_fill=lst.has_fill; s->fill=lst.fill;
                        s->has_stroke=lst.has_stroke; s->stroke=lst.stroke; s->stroke_w=lst.stroke_w;
                        float ax, ay; aff_pt(lm, cx, cy, &ax, &ay);
                        if (aff_axis_aligned(lm) && fabsf(lm.a) == fabsf(lm.d)) {
                            s->kind = (rr >= 0 || (rx == ry)) ? SV_SH_CIRCLE : SV_SH_ELLIPSE;
                            s->native = 1;
                            s->cx=ax; s->cy=ay;
                            s->x1 = fabsf(lm.a)*rx;   /* x1/y1 复用为 rx/ry */
                            s->y1 = fabsf(lm.d)*ry;
                            s->rr = fabsf(lm.a)*rx;
                        } else {
                            s->kind = SV_SH_POLY; s->closed = 1;
                            const int SEG = 48;
                            for (int i=0;i<SEG;i++){
                                float t = 6.2831853f*(float)i/SEG;
                                float ox,oy; aff_pt(lm, cx+rx*cosf(t), cy+ry*sinf(t), &ox,&oy);
                                sv_push_pt(s,ox,oy);
                            }
                        }
                    }
                }
            } else if (!strcmp(nm, "line")) {
                float x1=sv_num(p, e,"x1",0), y1=sv_num(p, e,"y1",0);
                float x2=sv_num(p, e,"x2",0), y2=sv_num(p, e,"y2",0);
                sv_shape_t *s = sv_new_shape(d);
                if (s) {
                    s->kind = SV_SH_POLY; s->closed = 0;
                    s->has_fill = 0;                          /* 线不填充 */
                    s->has_stroke=lst.has_stroke; s->stroke=lst.stroke; s->stroke_w=lst.stroke_w;
                    if (!s->has_stroke && !lst.has_fill) { s->has_stroke = 1; s->stroke = FX_RGB(0,0,0); }
                    float ox,oy; aff_pt(lm,x1,y1,&ox,&oy); sv_push_pt(s,ox,oy);
                    aff_pt(lm,x2,y2,&ox,&oy); sv_push_pt(s,ox,oy);
                }
            } else if (!strcmp(nm, "polyline") || !strcmp(nm, "polygon")) {
                const char *v; int l;
                if (sv_attr(p, e, "points", &v, &l)) {
                    sv_shape_t *s = sv_new_shape(d);
                    if (s) {
                        s->kind = SV_SH_POLY; s->closed = !strcmp(nm, "polygon");
                        s->has_fill = lst.has_fill && s->closed;
                        s->fill=lst.fill;
                        s->has_stroke=lst.has_stroke; s->stroke=lst.stroke; s->stroke_w=lst.stroke_w;
                        float a[128];
                        int cnt = sv_numlist(v, l, a, 128);
                        for (int i = 0; i + 1 < cnt; i += 2) {
                            float ox,oy; aff_pt(lm,a[i],a[i+1],&ox,&oy); sv_push_pt(s,ox,oy);
                        }
                    }
                }
            } else if (!strcmp(nm, "path")) {
                const char *v; int l;
                if (sv_attr(p, e, "d", &v, &l))
                    sv_path(d, v, l, lm, lst.has_fill, lst.fill, lst.has_stroke, lst.stroke, lst.stroke_w);
            }
        } else if (nm[0]) {
            d->skipped++;   /* 不认识的元素: 记一笔, 不报错也不假装画了 */
        }
        p = e + 1;
    }

    if (d->vw <= 0) d->vw = 100;
    if (d->vh <= 0) d->vh = 100;
    return d;
}

void fx_svg_free(fx_svg_t *s)
{
    if (!s) return;
    for (int i = 0; i < s->n; i++) free(s->sh[i].pts);
    free(s->sh);
    free(s);
}

int   fx_svg_shape_count(const fx_svg_t *s) { return s ? s->n : 0; }
int   fx_svg_skipped(const fx_svg_t *s)     { return s ? s->skipped : 0; }

int fx_svg_size(const fx_svg_t *s, float *vw, float *vh)
{
    if (!s) return 0;
    if (vw) *vw = s->vw;
    if (vh) *vh = s->vh;
    return 1;
}

/* 测试/调试用: 取第 i 个形状的点数(不暴露内部结构, 避免把内部布局变成 ABI) */
int fx_svg_shape_points(const fx_svg_t *s, int i)
{
    if (!s || i < 0 || i >= s->n) return -1;
    return s->sh[i].n;
}

/* 取第 i 个形状的包围盒(解析后的坐标, 即已应用 transform)。
 * 用途: 自检里断言 transform 真的生效了; 也可用于把图标对齐到基线。 */
int fx_svg_shape_bbox(const fx_svg_t *s, int i, float *x1, float *y1, float *x2, float *y2)
{
    if (!s || i < 0 || i >= s->n) return 0;
    const sv_shape_t *sh = &s->sh[i];
    float a=0,b=0,c=0,d=0;
    switch (sh->kind) {
    case SV_SH_RECT:
    case SV_SH_ROUNDRECT:
        a = sh->x1; b = sh->y1; c = sh->x2; d = sh->y2;
        break;
    case SV_SH_CIRCLE:
        a = sh->cx - sh->rr; b = sh->cy - sh->rr;
        c = sh->cx + sh->rr; d = sh->cy + sh->rr;
        break;
    case SV_SH_ELLIPSE:
        a = sh->cx - sh->x1; b = sh->cy - sh->y1;
        c = sh->cx + sh->x1; d = sh->cy + sh->y1;
        break;
    case SV_SH_POLY:
        if (sh->n <= 0) return 0;
        a = c = sh->pts[0]; b = d = sh->pts[1];
        for (int k = 1; k < sh->n; k++) {
            float px = sh->pts[k*2], py = sh->pts[k*2+1];
            if (px < a) a = px;
            if (px > c) c = px;
            if (py < b) b = py;
            if (py > d) d = py;
        }
        break;
    default: return 0;
    }
    if (x1) *x1 = a;
    if (y1) *y1 = b;
    if (x2) *x2 = c;
    if (y2) *y2 = d;
    return 1;
}

/* ===================== 绘制 ===================== */

void fx_svg_draw(const fx_svg_t *s, int x, int y, int w, int h)
{
    if (!s || w <= 0 || h <= 0 || s->vw <= 0 || s->vh <= 0) return;

    /* 等比缩放 + 居中(保持宽高比, 不拉伸变形 —— 图标被拉扁是最常见的"看着别扭") */
    float sx = (float)w / s->vw, sy = (float)h / s->vh;
    float sc = (sx < sy) ? sx : sy;
    float offx = x + (w - s->vw * sc) * 0.5f;
    float offy = y + (h - s->vh * sc) * 0.5f;

    for (int i = 0; i < s->n; i++) {
        const sv_shape_t *sh = &s->sh[i];
        int wpx = (int)(sh->stroke_w * sc + 0.5f);
        if (wpx < 1) wpx = 1;

        switch (sh->kind) {
        case SV_SH_RECT:
        case SV_SH_ROUNDRECT: {
            /* ⚠️ 端点约定必须转换: SVG 的 width 是【半开区间】[x, x+w),
             *    而框架的 fx_fill_rect 是【闭区间】[x1, x2]。
             *    直接写 x2 = x+w 会让每个矩形都多画 1px(v2.5 单测抓出来的)。
             *    正确做法: 先定起点, 再按【宽度】推进。 */
            int px1 = (int)(offx + sh->x1 * sc + 0.5f);
            int py1 = (int)(offy + sh->y1 * sc + 0.5f);
            int pw  = (int)((sh->x2 - sh->x1) * sc + 0.5f);
            int ph  = (int)((sh->y2 - sh->y1) * sc + 0.5f);
            if (pw < 1) pw = 1;
            if (ph < 1) ph = 1;
            int x1 = px1, y1 = py1, x2 = px1 + pw - 1, y2 = py1 + ph - 1;
            int r = (int)(sh->rx * sc + 0.5f);
            if (sh->has_fill) {
                fx_set_color(sh->fill);
                if (sh->kind == SV_SH_ROUNDRECT) fx_fill_rect_round(x1,y1,x2,y2,r); else fx_fill_rect(x1,y1,x2,y2);
            }
            if (sh->has_stroke) {
                fx_set_color(sh->stroke);
                if (sh->kind == SV_SH_ROUNDRECT) fx_draw_rect_round(x1,y1,x2,y2,r); else fx_draw_rect(x1,y1,x2,y2);
            }
            break; }
        case SV_SH_CIRCLE: {
            int cx = (int)(offx + sh->cx * sc), cy = (int)(offy + sh->cy * sc);
            int r  = (int)(sh->rr * sc + 0.5f);
            if (sh->has_fill)   { fx_set_color(sh->fill);   fx_fill_circle(cx,cy,r); }
            if (sh->has_stroke) { fx_set_color(sh->stroke); fx_draw_circle(cx,cy,r); }
            break; }
        case SV_SH_ELLIPSE: {
            int cx = (int)(offx + sh->cx * sc), cy = (int)(offy + sh->cy * sc);
            int rx = (int)(sh->x1 * sc + 0.5f), ry = (int)(sh->y1 * sc + 0.5f);
            if (sh->has_fill)   { fx_set_color(sh->fill);   fx_fill_ellipse(cx,cy,rx,ry); }
            if (sh->has_stroke) { fx_set_color(sh->stroke); fx_draw_ellipse(cx,cy,rx,ry); }
            break; }
        case SV_SH_POLY: {
            if (sh->n < 2) break;
            /* 折线点先缩放到屏幕坐标(用栈缓冲, 避免每帧 malloc) */
            int16_t buf[SV_MAX_PTS * 2];
            int n = sh->n;
            if (n > SV_MAX_PTS) n = SV_MAX_PTS;
            for (int k = 0; k < n; k++) {
                buf[k*2+0] = (int16_t)(offx + sh->pts[k*2+0] * sc);
                buf[k*2+1] = (int16_t)(offy + sh->pts[k*2+1] * sc);
            }
            if (sh->closed && sh->has_fill) { fx_set_color(sh->fill); fx_fill_polygon(buf, n); }
            if (sh->has_stroke) {
                fx_set_color(sh->stroke);
                if (sh->closed) fx_draw_polygon(buf, n);
                else for (int k = 0; k + 1 < n; k++) fx_draw_line(buf[k*2], buf[k*2+1], buf[k*2+2], buf[k*2+3]);
            }
            (void)wpx;
            break; }
        default: break;
        }
    }
}

/* ===================== 文件加载 ===================== */

fx_svg_t *fx_svg_load(const char *path)
{
    if (!path) return NULL;
    /* 走框架自己的文件接口, 这样 stub 后端的"内存文件系统"也能用 */
    int sz = 0;
    void *buf = fx_file_read(path, &sz);
    if (!buf || sz <= 0) { if (buf) fx_file_free(buf); return NULL; }
    char *txt = (char *)malloc((size_t)sz + 1);
    if (!txt) { fx_file_free(buf); return NULL; }
    memcpy(txt, buf, (size_t)sz); txt[sz] = 0;
    fx_file_free(buf);
    fx_svg_t *s = fx_svg_parse(txt);
    free(txt);
    return s;
}
