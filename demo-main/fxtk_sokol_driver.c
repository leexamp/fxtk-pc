/**
 * fxtk_sokol_driver.c — v2.4 sokol 后端 (替代 SDL2)
 *
 * 设计要点
 *  1. 主循环反转: sokol_app 拥有主循环, 而框架的 fx_driver_t 是【轮询】契约(ESP32/无头测试同款)。
 *     因此这里把 sapp 回调事件塞进环形队列, 由 touch_read/key_read/wheel_read 出队 —— 核心框架零改动。
 *  2. 顶点是【全程批处理】: 实心矩形/线段/三角形/文本/贴图/图片统一走一个动态顶点缓冲,
 *     顶点带颜色 → 换色不再打断批次(这是相对 SDL 逐色批的一大收益)。
 *     fx_poll 期间只往 CPU 侧数组追加顶点 + 记录命令(管线/纹理绑定/顶点范围), present 时一次上传 + 逐条 draw,
 *     顺序完全保持。
 *  3. 软件像素层(文字/抗锯齿/离屏画布/自定义绘制)沿用 SDL 驱动的做法: 攒进 fb 的脏区 → 上传到一张
 *     streaming 纹理(只上传脏区大小) → 作为一条贴图命令插入命令流, z 序天然正确。
 *  4. read_pixels 用 GL 后端直接 glReadPixels (截图/金图)。D3D11 后端暂不支持(返回 0)。
 */
/* v2.4: 统一走 OpenGL(GLCORE), Windows 也是 —— 两条理由:
 *  ① sokol 的 D3D11 后端要求另写 HLSL 着色器, 而本驱动全部着色器都是 GLSL 330;
 *     用 GLCORE 在 Windows 上(WGL)可以直接复用同一份源码, 不必维护两套着色器。
 *  ② read_pixels 走 glReadPixels, D3D11 后端下截图是失效的(金图回归会没有依据)。
 * 代价: Windows 侧需要系统自带 opengl32.dll(所有 Windows 都有) —— 正好满足"单 exe 无第三方 DLL"。 */
#define SOKOL_GLCORE

#define SOKOL_APP_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_LOG_IMPL
/* 注意顺序: sokol 各头文件的"实现段"不在 include guard 内, 同一 TU 里绝不能包含两次。
 * fxtk_sokol.h 会带出 sokol_gfx.h(声明+实现), 所以这里不再单独 include 它。 */
#include "fxtk_sokol.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "raymarch_fs330.h"   /* 由 tools/gen_raymarch_glsl330.py 生成 */
#include "fxtk_backends.h"   /* fx_time_ms: 统一计时, 免依赖 sokol_time */
#include "fxtk_image.h"      /* v2.4: fx_quad_corner_weights (GPU 真透视四边形的每角权重) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if !defined(_WIN32) && !defined(SOKOL_GLCORE)
  #error "unexpected backend"
#endif

/* ================= 顶点/命令表 ================= */

/* 顶点: pos(NDC) + uv + color + 4 个 SDF 参数槽。
 * SDF 参数(v2.4 抗锯齿)复用同一条顶点流: 普通四边形填 0, 圆角矩形填 (半宽,半高,圆角,描边宽) —
 * 这样只需多一条管线, 不必再开一个顶点缓冲/第二套命令流。其局部坐标借用 uv 传。 */
/* 【字段顺序必须与管线属性声明顺序一致】sokol 按 attrs[] 的先后紧密排布偏移:
 * pos(0) uv(8) color(16) sdf(20) —— 把 rgba 写到结构体末尾会让颜色属性从 32 读到 16 处,
 * 结果整屏全黑(踩过两次: 第一次漏了 attr, 第二次字段顺序反了)。 */
typedef struct { float x, y, u, v; uint32_t rgba; float s0, s1, s2, s3; } vtx_t;

#define VB_MAX   262144     /* 顶点数上限 (~4MB) */
#define CMD_MAX  8192
#define TEX_MAX  256        /* 每帧可用的纹理绑定数(文本纹理等) */

static vtx_t  s_vb[VB_MAX];
static int    s_vb_n = 0;
/* 索引绘制: sokol 的 sg_draw(base_element) 对【非索引】绘制不做顶点偏移, 因此批次偏移
 * 必须走索引缓冲(索引偏移 = base_element)。这也是 sokol 的惯用做法。 */
#define IB_MAX  (VB_MAX * 2)
static uint32_t s_ib[IB_MAX];
static int      s_ib_n = 0;

typedef struct {
    int  first, count;
    int  pip;               /* 0 = 实心, 1 = 贴图, 2 = 光追 */
    int  tex;               /* 纹理槽位 (-1 = 无) */
    /* v2.4: 每条命令携带裁剪矩形 —— 框架通过 set_clip_rect 表达裁剪(画布/滚动/标签页),
     * 驱动必须落到 GPU scissor 上, 否则线宽外扩的 0.5px 会溢出容器(实测: 波形溢出画布 1~2px)。 */
    int  cx1, cy1, cx2, cy2;
    float time;             /* pip=2 光追用 */
} cmd_t;
static int s_clip_x1 = 0, s_clip_y1 = 0, s_clip_x2 = 32767, s_clip_y2 = 32767;
static cmd_t  s_cmd[CMD_MAX];
static int    s_cmd_n = 0;
static sg_buffer s_ibuf;

/* 每帧的纹理槽位: 记录 sg_image + 尺寸, 用于构造 bindings */
typedef struct { sg_image img; sg_view view; sg_sampler smp; int w, h; } tex_slot_t;
static tex_slot_t s_tex[TEX_MAX];
static int s_tex_n = 0;

/* ================= 驱动状态 ================= */

static int s_w = 480, s_h = 272;
static sg_pipeline s_pip_sdf, s_pip_solid, s_pip_tex, s_pip_raymarch, s_pip_quad;
static sg_shader s_sh_raymarch;
static sg_buffer   s_vbuf;
static sg_sampler  s_smp_nearest, s_smp_linear;

/* 软件像素层 */
static uint32_t *s_fb = NULL;
static int s_fb_w = 0, s_fb_h = 0;
static int s_dx0 = 0, s_dy0 = 0, s_dx1 = 0, s_dy1 = 0, s_dirty = 0;
static sg_image s_px_img; static sg_view s_px_view;   /* 脏区纹理 + 视图 */
static int s_px_w = 0, s_px_h = 0;
static uint32_t *s_px_stage = NULL;
static int s_px_stage_cap = 0;

/* 持久离屏画布: 框架用脏矩形重绘, 静态内容不会每帧重画 —— 因此渲染目标必须跨帧保留。
 * sokol_app 是双缓冲, 直接画交换链会让静态部分丢失, 所以与 SDL 驱动同构:
 * 先画进这张持久纹理, 再整屏贴到交换链。 */
static sg_image s_canvas_img;
static sg_view  s_canvas_attach, s_canvas_tex;
static sg_buffer s_blit_vb, s_blit_ib;
static int s_canvas_w = 0, s_canvas_h = 0;

/* 首帧/窗口尺寸变化后需要清一次屏; 其余帧必须 LOAD(保留上一帧内容) ——
 * 框架采用脏矩形重绘, 静态部分不会每帧重画, 若每帧 CLEAR 会让它们变黑。 */
static int s_need_clear = 1;

/* FPS 统计 (fxtk_sokol_frame 用; 声明须早于使用) */
static int s_fps = 0, s_fps_n = 0;

/* ---------- v2.4 性能诊断 (FXTK_STAT=1) ----------
 * 只报 fps 会骗人: fps 60 也可能"手感延迟"。这里把一帧拆成四段分别计时 ——
 *   ①输入排队(事件入队→框架取走) ②框架逐帧(fx_poll) ③像素层上传(px_flush) ④GPU 提交(含 vsync 等待)
 * 单位为微秒, 每秒打印一次 max/avg, 用来定位"卡"到底卡在哪一段。 */
extern int fxtk_text_created, fxtk_text_evicted;   /* 由 fxtk_font_stb.c 提供 */
#if !defined(_WIN32)
#include <time.h>
#endif
static uint32_t now_us(void)
{
#if defined(_WIN32)
    return (uint32_t)fx_time_ms() * 1000u;      /* Windows 取毫秒精度, 足以定位 >1ms 的问题 */
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000000u + (uint32_t)(ts.tv_nsec / 1000);
#endif
}
uint32_t fxtk_sokol_now_us(void) { return now_us(); }

static uint32_t s_lat_us_max = 0, s_lat_us_sum = 0; static int s_lat_n = 0;
static long s_merge_n = 0; static int s_depth_max = 0;      /* ①输入排队 */
static uint32_t s_frame_us_max = 0, s_frame_us_sum = 0;                          /* ②③④整帧 */
static uint32_t s_px_us_max = 0; static long s_px_area = 0; static int s_px_calls = 0;  /* ③像素上传 */
static uint32_t s_sub_us_max = 0, s_sub_us_sum = 0;

static uint32_t s_stat_t0 = 0;               /* FXTK_STAT 的每秒汇总基准 */
void fxtk_sokol_note_frame(uint32_t us)      /* main 在 fx_poll + fxtk_sokol_frame 外包一层计时 */
{
    if (us > s_frame_us_max) s_frame_us_max = us;
    s_frame_us_sum += us;
}

/* 当前 set_window 状态 (软件像素按行推送) */
static uint16_t cur_x0, cur_y0, cur_w; static uint32_t cur_idx = 0;

/* 图片上传用纹理 (blit_img) */
/* 图片上传用纹理【池】。
 * 为什么是池而不是一张: sokol 校验 "同一帧同一图片只能 sg_update_image 一次"(VALIDATE_UPDIMG_ONCE),
 * 而一帧里完全可能画多张图(图片页 3 张、画板页 canvas 重绘两次…) —— 共用一张就会 panic 直接 abort,
 * 表现就是"图片页/画板闪退"。改成每帧按需取一张, 每次 blit 用不同图片。
 * 暂存缓冲仍然共用一张: GL 后端的 sg_update_image 是【立即】glTexSubImage2D, 不会被后续 blit 覆盖。 */
#define IMG_SLOTS 16
static sg_image s_img_tex[IMG_SLOTS]; static sg_view s_img_view[IMG_SLOTS];
static int s_img_w[IMG_SLOTS], s_img_h[IMG_SLOTS];
static int s_img_slot = 0;                 /* 帧内轮转游标, 每帧归零 */
static int s_img_over = 0, s_img_used_max = 0, s_img_used = 0;   /* 诊断: 本帧用了几个 */
static int s_sdf_n = 0, s_sdf_max = 0;
int s_dbg_calls = 0;     /* 诊断: 本帧 SDF 圆角矩形数(抗锯齿档≥1 时应 >0) */
static uint32_t *s_img_stage = NULL; static int s_img_cap = 0;

/* ================= 输入事件队列 (按类型分队列) =================
 * 注意: 框架按类型分别轮询(touch/key/wheel), 早期实现用单队列+延迟数组,
 * 会在某类型轮询时把其它类型的事件吞掉 → 输入完全失效。这里分成三个环形队列。 */
#define EVQ_MAX 256
typedef struct { int x, y, p, edge; uint32_t t; } ev_touch_t;  /* t=入队时刻(us); edge=按下/抬起边沿(不可合并) */
typedef struct { int x, y, dy; } ev_wheel_t;

static ev_touch_t s_q_touch[EVQ_MAX]; static int s_qt_h = 0, s_qt_t = 0;
static ev_wheel_t s_q_wheel[EVQ_MAX]; static int s_qw_h = 0, s_qw_t = 0;
static fx_keyev_t s_q_key[EVQ_MAX];   static int s_qk_h = 0, s_qk_t = 0;

/* 入队 touch/鼠标事件。
 * 【关键】框架每帧只消费 1 个 touch 事件(fx_poll 里是 if 而不是 while, 与 SDL 驱动同款语义),
 * 而 sokol_app 会把鼠标移动按系统速率逐个投递 —— 若逐个入队, 快速拖动一帧能积压十几个事件,
 * 界面用的坐标就落后指针 100~200ms(实测排队 max=216ms/avg=101ms), 手感就是"卡"。
 * SDL 驱动不排队, 它只保存【最新】鼠标坐标; 这里用等价做法: 未按键的移动事件直接覆盖队尾,
 * 保证每帧最多推进一个"最新位置"; 按下/抬起是边沿, 必须原样入队(否则点击落点会被拖走)。 */
static void q_push_touch(int x, int y, int p)
{
    if (s_qt_h != s_qt_t) {                     /* 队列非空: 看队尾能否合并 */
        int tl = (s_qt_t + EVQ_MAX - 1) % EVQ_MAX;
        if (!s_q_touch[tl].edge && s_q_touch[tl].p == p) {
            s_q_touch[tl].x = x; s_q_touch[tl].y = y; s_q_touch[tl].t = now_us();
            s_merge_n++;
            return;                             /* 合并: 只保留最新坐标 */
        }
    }
    {   int n = (s_qt_t + 1) % EVQ_MAX;
        if (n == s_qt_h) { s_qt_h = (s_qt_h + 1) % EVQ_MAX; }   /* 满: 丢最旧, 不阻塞 */
    }
    s_q_touch[s_qt_t].x = x; s_q_touch[s_qt_t].y = y; s_q_touch[s_qt_t].p = p;
    s_q_touch[s_qt_t].edge = 0; s_q_touch[s_qt_t].t = now_us();
    s_qt_t = (s_qt_t + 1) % EVQ_MAX;
}

/* 同上: 边沿事件(按下/抬起)入队, 不参与合并 */
static void q_push_touch_edge(int x, int y, int p)
{
    int n = (s_qt_t + 1) % EVQ_MAX;
    if (n == s_qt_h) { s_qt_h = (s_qt_h + 1) % EVQ_MAX; }
    s_q_touch[s_qt_t].x = x; s_q_touch[s_qt_t].y = y; s_q_touch[s_qt_t].p = p;
    s_q_touch[s_qt_t].edge = 1; s_q_touch[s_qt_t].t = now_us();
    s_qt_t = n;
}
static void q_push_wheel(int x, int y, int dy)
{
    if (s_qw_h != s_qw_t) {                    /* 同一格内的连续滚动累加(框架每帧也只看一个滚轮事件) */
        int tl = (s_qw_t + EVQ_MAX - 1) % EVQ_MAX;
        if (s_q_wheel[tl].x == x && s_q_wheel[tl].y == y) { s_q_wheel[tl].dy += dy; return; }
    }
    int n = (s_qw_t + 1) % EVQ_MAX;
    if (n == s_qw_h) { s_qw_h = (s_qw_h + 1) % EVQ_MAX; }
    s_q_wheel[s_qw_t].x = x; s_q_wheel[s_qw_t].y = y; s_q_wheel[s_qw_t].dy = dy;
    s_qw_t = n;
}
static void q_push_key(const fx_keyev_t *ev)
{
    int n = (s_qk_t + 1) % EVQ_MAX;
    if (n == s_qk_h) { s_qk_h = (s_qk_h + 1) % EVQ_MAX; }
    s_q_key[s_qk_t] = *ev;
    s_qk_t = n;
}

/* ================= 顶点追加 ================= */

static int cmd_new(int pip, int tex)
{
    if (s_cmd_n >= CMD_MAX) return -1;
    s_cmd[s_cmd_n].first = s_ib_n;
    s_cmd[s_cmd_n].count = 0;
    s_cmd[s_cmd_n].pip = pip;
    s_cmd[s_cmd_n].tex = tex;
    s_cmd[s_cmd_n].cx1 = s_clip_x1; s_cmd[s_cmd_n].cy1 = s_clip_y1;
    s_cmd[s_cmd_n].cx2 = s_clip_x2; s_cmd[s_cmd_n].cy2 = s_clip_y2;
    s_cmd[s_cmd_n].time = 0.0f;
    return s_cmd_n++;
}

/* 颜色打包: 顶点属性 UBYTE4N 按内存字节序读作 R,G,B,A。
 * 我们的像素/颜色常量是 0xAARRGGBB(小端内存 = B,G,R,A) → 直接写入会让 R/B 互换
 * (实测: 黄色波形线被画成蓝色)。这里显式交换, 得到 0xAABBGGRR。 */
/* 注意: 本版 sokol 里两条绘制路径的字节序要求【不同】——
 *   顶点属性 UBYTE4N: 直接写 0xAARRGGBB 即正确(实测);
 *   RGBA8 纹理上传(像素层/图片): 必须交换 R/B, 否则同色区域会出现反色子矩形(实测)。
 * 这是"颜色有问题"的根因, 别再当成冗余代码删掉。 */
static inline uint32_t rgba_pack(uint32_t rgb)
{
    return 0xFF000000u | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);
}

/* 像素坐标 → NDC (在 CPU 侧算好, 免掉 uniform block 及其名字查找/420pack 扩展等一整类坑) */
static void vtx_push_sdf(float x, float y, float u, float v, uint32_t c,
                        float s0, float s1, float s2, float s3)
{
    if (s_vb_n >= VB_MAX) return;
    float rw = (float)(s_w > 0 ? s_w : 1), rh = (float)(s_h > 0 ? s_h : 1);
    s_vb[s_vb_n].x = x / rw * 2.0f - 1.0f;
    s_vb[s_vb_n].y = y / rh * 2.0f - 1.0f;
    s_vb[s_vb_n].u = u; s_vb[s_vb_n].v = v;
    s_vb[s_vb_n].s0 = s0; s_vb[s_vb_n].s1 = s1; s_vb[s_vb_n].s2 = s2; s_vb[s_vb_n].s3 = s3;
    s_vb[s_vb_n].rgba = rgba_pack(c);
    s_vb_n++;
}

static void vtx_push(float x, float y, float u, float v, uint32_t c)
{
    if (s_vb_n >= VB_MAX) return;
    float rw = (float)(s_w > 0 ? s_w : 1), rh = (float)(s_h > 0 ? s_h : 1);
    s_vb[s_vb_n].x = x / rw * 2.0f - 1.0f;
    /* sokol 的默认视口是 top-left 原点(内部会翻转), 因此 NDC 的 +1 对应窗口【下边】:
     * 屏幕 y=0 必须映射到 NDC -1。实测踩坑: 原写法让整幅画面上下颠倒。 */
    s_vb[s_vb_n].y = y / rh * 2.0f - 1.0f;
    s_vb[s_vb_n].u = u; s_vb[s_vb_n].v = v;
    s_vb[s_vb_n].s0 = 0.0f; s_vb[s_vb_n].s1 = 0.0f; s_vb[s_vb_n].s2 = 0.0f; s_vb[s_vb_n].s3 = 0.0f;
    /* 顶点色字节序(2026-09 修正, 这条以前写错了):
     * sokol 的 SG_VERTEXFORMAT_UBYTE4N 把【内存里第 0 个字节】喂给 vec4 的 x 分量。
     * 小端下直接写 0xAARRGGBB 的内存布局是 B,G,R,A → 着色器拿到 vec4(B,G,R,A),
     * 片元直接 frag_color = vcol 就会让整屏 R/B 互换(红按钮显示成蓝按钮)。
     * 与纹理路径同理, 这里也必须搬一次字节: 存成 0xAABBGGRR 后内存才是 R,G,B,A。
     * 老注释"直接写即正确"是被【同样互换了两次的截图回读】骗出来的结论, 勿再改回。 */
    s_vb[s_vb_n].rgba = rgba_pack(c);
    s_vb_n++;
}

static void emit_quad(int pip, int tex, float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1, uint32_t c);
/* v2.4: 发出一个 SDF 四边形(圆角矩形填充 border=0 / 描边 border>0)。
 * uv 通道传【相对矩形中心的像素偏移】—— 光栅化在片元中心线性插值, 于是片元里的 uv 就是
 * 它相对中心的局部坐标, 交给片元着色器求到边界的距离即可。 */
static void emit_round(int x1, int y1, int x2, int y2, int rad, int border, uint32_t c)
{
    if (x2 < x1 || y2 < y1) return;
    float fx0 = (float)x1, fy0 = (float)y1;
    float fx1 = (float)x2 + 1.0f, fy1 = (float)y2 + 1.0f;      /* 右下边界取开区间, 与 fill_rect 一致 */
    float cx = (fx0 + fx1) * 0.5f, cy = (fy0 + fy1) * 0.5f;
    float hw = (fx1 - fx0) * 0.5f, hh = (fy1 - fy0) * 0.5f;
    if (rad * 2 > (int)(hw * 2)) rad = (int)hw;
    if (rad * 2 > (int)(hh * 2)) rad = (int)hh;
    if (getenv("FXTK_SDFCMP")) emit_quad(0, -1, fx0, fy0, fx1, fy1, 0,0,0,0, 0xFF00FF00u);
    /* 与 emit_quad 同款合并: 相邻 SDF 四边形共用一条命令(否则一个控件一条 draw call) */
    if (s_cmd_n == 0 || s_cmd[s_cmd_n - 1].pip != 3 || s_cmd[s_cmd_n - 1].tex != -1) {
        if (cmd_new(3, -1) < 0) return;
    }
    s_sdf_n++;
    if (s_vb_n + 4 > VB_MAX || s_ib_n + 6 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    float r = (float)rad, bw = (float)border;
    vtx_push_sdf(fx0, fy0, fx0 - cx, fy0 - cy, c, hw, hh, r, bw);
    vtx_push_sdf(fx1, fy0, fx1 - cx, fy0 - cy, c, hw, hh, r, bw);
    vtx_push_sdf(fx1, fy1, fx1 - cx, fy1 - cy, c, hw, hh, r, bw);
    vtx_push_sdf(fx0, fy1, fx0 - cx, fy1 - cy, c, hw, hh, r, bw);
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 2; s_ib[s_ib_n++] = b + 3;
    cm->count += 6;
}

/* 追加一个矩形(两三角); pip/tex 与"当前命令"一致则续用, 否则新建命令 */
static void emit_quad(int pip, int tex, float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1, uint32_t c)
{
    if (s_cmd_n == 0 || s_cmd[s_cmd_n - 1].pip != pip || s_cmd[s_cmd_n - 1].tex != tex) {
        if (cmd_new(pip, tex) < 0) return;
    }
    if (s_vb_n + 4 > VB_MAX || s_ib_n + 6 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    vtx_push(x0, y0, u0, v0, c); vtx_push(x1, y0, u1, v0, c);
    vtx_push(x1, y1, u1, v1, c); vtx_push(x0, y1, u0, v1, c);
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 2; s_ib[s_ib_n++] = b + 3;
    cm->count += 6;
}

static int tex_alloc(sg_image img, sg_view view, int w, int h)
{
    for (int i = 0; i < s_tex_n; i++)          /* 同一纹理复用绑定点:
                                                * 旧实现每次绘制都新占一槽, 文本多的页面会耗尽
                                                * TEX_MAX=256 → 后续绘制被静默跳过(表现为内容缺失) */
        if (s_tex[i].img.id == img.id) return i;
    if (s_tex_n >= TEX_MAX) { s_tex[0].img = img; s_tex[0].view = view; s_tex[0].w = w; s_tex[0].h = h; return 0; }
    s_tex[s_tex_n].img = img;
    s_tex[s_tex_n].view = view;
    s_tex[s_tex_n].smp = s_smp_nearest;
    s_tex[s_tex_n].w = w; s_tex[s_tex_n].h = h;
    return s_tex_n++;
}

/* ================= 软件像素层 ================= */

static void fb_ensure(void)
{
    if (s_fb_w == s_w && s_fb_h == s_h) return;
    free(s_fb);
    s_fb = (uint32_t *)calloc((size_t)s_w * s_h, 4);
    s_fb_w = s_w; s_fb_h = s_h;
    s_dirty = 0;
}

static void px_mark(int x, int y)
{
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) return;
    if (s_fb[(size_t)y * s_w + x] == 0) s_fb[(size_t)y * s_w + x] = 0xFF000000u;   /* 标记已写(含透明黑) */
}

/* 像素层: 脏区子矩形直接 transient 上传 (sokol 允许同帧多次 write_transient, 只要绑定前完成)。
 * 旧实现每帧把整屏(1080p 约 200 万像素)转一遍 RGBA 再上传 4MB —— 拖动滑块时延迟 300ms 级的元凶。 */
static void px_ensure_img(void)
{
    if (s_px_img.id != SG_INVALID_ID && s_px_w == s_w && s_px_h == s_h) return;
    if (s_px_img.id != SG_INVALID_ID) { sg_destroy_view(s_px_view); sg_destroy_image(s_px_img); }
    s_px_img = sg_make_image(&(sg_image_desc){
        .width = s_w, .height = s_h, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.write_transient = true,          /* 同帧多次子矩形写入 */
        .label = "fxtk-pixels" });
    s_px_view = sg_make_view(&(sg_view_desc){ .texture = { .image = s_px_img }, .label = "fxtk-pixels-view" });
    s_px_w = s_w; s_px_h = s_h;
    s_dirty = 0;
}

static void px_flush(void)
{
    uint32_t t_px0 = now_us();
    if (!s_dirty || !s_fb) { s_dirty = 0; cur_idx = 0; return; }
    int x0 = s_dx0, y0 = s_dy0, x1 = s_dx1, y1 = s_dy1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= s_fb_w) x1 = s_fb_w - 1;
    if (y1 >= s_fb_h) y1 = s_fb_h - 1;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    s_dirty = 0; cur_idx = 0;
    s_px_area += (long)w * h; s_px_calls++;
    if (w < 1 || h < 1) return;
    px_ensure_img();
    if (s_px_stage_cap < w * h) {
        free(s_px_stage);
        s_px_stage = (uint32_t *)malloc((size_t)w * h * 4);
        if (!s_px_stage) { s_px_stage_cap = 0; return; }
        s_px_stage_cap = w * h;
    }
    if (!s_px_stage) return;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = &s_fb[(size_t)(y0 + y) * s_fb_w + x0];
        uint32_t *dst = &s_px_stage[(size_t)y * w];
        for (int x = 0; x < w; x++) {
            uint32_t c = src[x];
            dst[x] = (c == 0u) ? 0u : rgba_pack(c & 0xFFFFFFu);   /* 未写过 → 透明 */
        }
    }
    sg_write_image_transient(&(sg_write_image_desc){
        /* bytes_per_row 必须显式给: 不然 sokol 按后端行对齐(实测 256B)推算所需大小,
         * 遇到 43*4=172 这种非对齐行宽就判越界 → panic (图形/图片页闪退的根因)。 */
        /* 三个字段都必须给: bytes_per_slice 缺省为 0 会被校验拒绝(图形/图片页闪退根因);
         * 校验公式 size = bytes_per_slice * num_slices ≤ data.size */
        .src  = { .data = { s_px_stage, (size_t)w * h * 4 },
                  .bytes_per_row = w * 4,
                  .bytes_per_slice = (size_t)w * 4 * h },
        .dst  = { .image = s_px_img, .mip_level = 0, .x = x0, .y = y0, .slice = 0 },
        .size = { .width = w, .height = h, .num_slices = 1 } });
    int slot = tex_alloc(s_px_img, s_px_view, s_w, s_h);
    if (slot >= 0)
        emit_quad(1, slot, (float)x0, (float)y0, (float)(x1 + 1), (float)(y1 + 1),
                  (float)x0 / (float)s_w, (float)y0 / (float)s_h,
                  (float)(x1 + 1) / (float)s_w, (float)(y1 + 1) / (float)s_h, 0xFFFFFFFFu);
    for (int y = y0; y <= y1; y++) memset(&s_fb[(size_t)y * s_fb_w + x0], 0, (size_t)w * 4);
    { uint32_t d = now_us() - t_px0; if (d > s_px_us_max) s_px_us_max = d; }   /* ③像素层 */
}

/* ================= fx_driver_t 钩子 ================= */

static int drv_init(void)
{
    if (getenv("FXTK_NO_QUADGPU")) fx_sokol_driver.draw_image_quad = NULL;   /* A/B: 强制走 CPU 逆单应 */
    sg_desc desc = {0};
    /* 环境默认值与我们的管线保持一致: 2D UI 不需要深度缓冲。
     * (交换链的 depth_format 取自环境默认值, 不一致会让 sokol 校验直接 panic) */
    {
        sg_environment env = sglue_environment();
        env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        env.defaults.depth_format = SG_PIXELFORMAT_NONE;
        env.defaults.sample_count = 1;
        desc.environment = env;
    }
    desc.logger.func = slog_func;
    desc.buffer_pool_size = 64;
    desc.image_pool_size = 512;         /* 文本纹理较多 */
    desc.sampler_pool_size = 16;
    desc.shader_pool_size = 16;
    desc.pipeline_pool_size = 16;
    desc.view_pool_size = 512;
    desc.uniform_buffer_size = 64 * 1024;
    sg_setup(&desc);

    s_w = sapp_width(); s_h = sapp_height();
    s_smp_nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE, .label = "fxtk-nearest" });
    s_smp_linear = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE, .label = "fxtk-linear" });

    s_vbuf = sg_make_buffer(&(sg_buffer_desc){
        .size = sizeof(vtx_t) * VB_MAX, .usage.vertex_buffer = true, .usage.dynamic_update = true,
        .label = "fxtk-vertices" });
    s_ibuf = sg_make_buffer(&(sg_buffer_desc){
        .size = sizeof(uint32_t) * IB_MAX, .usage.index_buffer = true, .usage.dynamic_update = true,
        .label = "fxtk-indices" });

    /* 顶点布局两端共用: pos(0) + uv(1) + color(2) */
    sg_vertex_layout_state layout = {0};
    layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;    /* position (NDC) */
    layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;    /* uv / SDF 局部坐标 */
    layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;   /* 颜色 */
    /* v2.4: 第 4 个属性是 SDF 参数。sokol 按声明顺序紧密排列属性偏移 ——
     * 漏掉它, color 就会被放在偏移 16 而结构体里实际在 36 → 整屏全黑(踩过)。 */
    layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4;    /* (半宽, 半高, 圆角, 描边宽) */
    layout.buffers[0].stride = sizeof(vtx_t);

    static const char *vs_src =
        "#version 330\n"
        "layout(location=0) in vec2 position;\n"   /* 已是 NDC */
        "layout(location=1) in vec2 texcoord0;\n"
        "layout(location=2) in vec4 color0;\n"
        "layout(location=3) in vec4 sdf0;\n"
        "out vec2 uv; out vec4 vcol; out vec4 vsdf;\n"
        "void main() { uv = texcoord0; vcol = color0; vsdf = sdf0; gl_Position = vec4(position, 0.0, 1.0); }\n";

    sg_shader_desc shd;
    memset(&shd, 0, sizeof(shd));
    shd.vertex_func.source = vs_src;
    shd.vertex_func.entry = "main";
    shd.attrs[0].glsl_name = "position";
    shd.attrs[1].glsl_name = "texcoord0";
    shd.attrs[2].glsl_name = "color0";
    shd.attrs[3].glsl_name = "sdf0";

    static const char *fs_solid_src =
        "#version 330\n"
        "in vec2 uv; in vec4 vcol; out vec4 frag_color;\n"
        "void main() { frag_color = vcol; }\n";
    shd.fragment_func.source = fs_solid_src;
    shd.fragment_func.entry = "main";
    sg_shader sh_solid = sg_make_shader(&shd);

    static const char *fs_tex_src =
        "#version 330\n"
        "uniform sampler2D u_tex;\n"
        "in vec2 uv; in vec4 vcol; out vec4 frag_color;\n"
        "void main() { frag_color = texture(u_tex, uv) * vcol; }\n";
    shd.fragment_func.source = fs_tex_src;
    shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.texture_sampler_pairs[0].view_slot = 0;
    shd.texture_sampler_pairs[0].sampler_slot = 0;
    shd.texture_sampler_pairs[0].glsl_name = "u_tex";
    sg_shader sh_tex = sg_make_shader(&shd);

    sg_shader sh_sdf = {0};
    /* SDF 圆角矩形/描边: 片元按到边界的有符号距离求覆盖度 → 边缘天然抗锯齿, 且每控件只需一次 draw。
     * fwidth() 给出该像素的距离梯度, 用它做 1px 羽化(距离场抗锯齿的标准做法)。 */
    static const char *fs_sdf_src =
        "#version 330\n"
        "in vec2 uv; in vec4 vcol; in vec4 vsdf; out vec4 frag_color;\n"
        "float sd_rbox(vec2 p, vec2 h, float r) {\n"
        "    vec2 q = abs(p) - h + vec2(r);\n"
        "    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;\n"
        "}\n"
        "/* 胶囊(线段 + 圆头): 用 vsdf.z < 0 复用同一条管线, |z| 即半宽 */\n"
        "float sd_capsule(vec2 p, float half_len, float hw) {\n"
        "    p.x -= clamp(p.x, -half_len, half_len);\n"
        "    return length(p) - hw;\n"
        "}\n"
        "void main() {\n"
        "    float d;\n"
        "    if (vsdf.z < 0.0) d = sd_capsule(uv, vsdf.x, -vsdf.z);\n"
        "    else              d = sd_rbox(uv, vsdf.xy, vsdf.z);\n"
        "    if (vsdf.w > 0.0) d = abs(d) - vsdf.w * 0.5;\n"   /* 描边/羽化宽度: 以边界为中心 */
        "    float aa = max(fwidth(d), 0.0001);\n"
        "    float cov = clamp(0.5 - d / aa, 0.0, 1.0);\n"
        "    if (cov <= 0.0) discard;\n"
        "    frag_color = vec4(vcol.rgb, vcol.a * cov);\n"
        "}\n";
    {   /* 注意: 不能复用上面带 u_tex 视图/采样器绑定的 shd —— SDF 管线不绑纹理,
         * 复用会让 sokol 校验 "view/sampler binding is missing" 直接 panic。 */
        sg_shader_desc sd;
        memset(&sd, 0, sizeof(sd));
        sd.vertex_func.source = vs_src;
        sd.vertex_func.entry = "main";
        sd.fragment_func.source = fs_sdf_src;
        sd.fragment_func.entry = "main";
        sd.attrs[0].glsl_name = "position";
        sd.attrs[1].glsl_name = "texcoord0";
        sd.attrs[2].glsl_name = "color0";
        sd.attrs[3].glsl_name = "sdf0";
        sh_sdf = sg_make_shader(&sd);
    }

    sg_pipeline_desc pd = {0};
    pd.layout = layout;
    pd.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pd.index_type = SG_INDEXTYPE_UINT32;
    pd.depth.pixel_format = SG_PIXELFORMAT_NONE;      /* 2D UI 不需要深度缓冲 */
    pd.depth.write_enabled = false;
    pd.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pd.color_count = 1;
    pd.colors[0].blend.enabled = true;
    pd.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pd.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pd.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pd.shader = sh_solid; pd.label = "fxtk-solid";
    s_pip_solid = sg_make_pipeline(&pd);
    pd.shader = sh_tex; pd.label = "fxtk-tex";
    s_pip_tex = sg_make_pipeline(&pd);
    {   sg_pipeline_desc spd = {0};
        spd.layout = layout; spd.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        spd.index_type = SG_INDEXTYPE_UINT32;
        spd.depth.pixel_format = SG_PIXELFORMAT_NONE;
        spd.depth.write_enabled = false; spd.depth.compare = SG_COMPAREFUNC_ALWAYS;
        spd.color_count = 1;
        spd.colors[0].blend.enabled = true;
        spd.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        spd.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        spd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        spd.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        spd.shader = sh_sdf; spd.label = "fxtk-sdf";
        s_pip_sdf = sg_make_pipeline(&spd);
    }
    /* v2.4 P4: 真透视四边形管线 —— 顶点着色器把"每角透视权重 d_i"放进 gl_Position.w,
     * 硬件于是按透视校正插值 uv(片元着色器与普通贴图完全相同, 直接复用 fs_tex_src)。
     * 这是"两个三角形各做仿射 → 对角缝"的正解: 单次 draw, 无缝隙。 */
    {
        static const char *vs_quad_src =
            "#version 330\n"
            "layout(location=0) in vec2 position;\n"
            "layout(location=1) in vec2 texcoord0;\n"
            "layout(location=2) in vec4 color0;\n"
            "layout(location=3) in vec4 sdf0;\n"      /* sdf0.x = 该角权重 */
            "out vec2 uv; out vec4 vcol; out vec4 vsdf;\n"
            "void main() { uv = texcoord0; vcol = color0; vsdf = sdf0;\n"
            "    gl_Position = vec4(position * sdf0.x, 0.0, sdf0.x); }\n";
        sg_shader_desc qd;
        memset(&qd, 0, sizeof(qd));
        qd.vertex_func.source = vs_quad_src;
        qd.vertex_func.entry = "main";
        qd.fragment_func.source = fs_tex_src;
        qd.fragment_func.entry = "main";
        qd.attrs[0].glsl_name = "position";
        qd.attrs[1].glsl_name = "texcoord0";
        qd.attrs[2].glsl_name = "color0";
        qd.attrs[3].glsl_name = "sdf0";
        qd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        qd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        qd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        qd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        qd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        qd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        qd.texture_sampler_pairs[0].view_slot = 0;
        qd.texture_sampler_pairs[0].sampler_slot = 0;
        qd.texture_sampler_pairs[0].glsl_name = "u_tex";
        sg_shader sh_quad = sg_make_shader(&qd);
        sg_pipeline_desc qpd;
        qpd = pd;                       /* 与 2D 管线同状态, 只换着色器 */
        qpd.shader = sh_quad; qpd.label = "fxtk-quadwarp";
        s_pip_quad = sg_make_pipeline(&qpd);
    }

    /* ---- 光追管线: 同一个全屏四边形, 片元着色器做光线步进 ---- */
    {
        static const char *vs_rm =
            "#version 330\n"
            "layout(location=0) in vec2 position;\n"    /* 已是 NDC */
            "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
        sg_shader_desc rs;
        memset(&rs, 0, sizeof(rs));
        rs.vertex_func.source = vs_rm;
        rs.vertex_func.entry = "main";
        rs.fragment_func.source = RAYMARCH_FS330;
        rs.fragment_func.entry = "main";
        rs.attrs[0].glsl_name = "position";
        rs.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
        rs.uniform_blocks[0].size = 20;              /* vec4 u_rect(16) + float u_time(4) —— 必须与成员布局一致 */
        rs.uniform_blocks[0].glsl_uniforms[0].glsl_name = "u_rect";
        rs.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
        rs.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
        rs.uniform_blocks[0].glsl_uniforms[1].glsl_name = "u_time";
        rs.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT;
        rs.uniform_blocks[0].glsl_uniforms[1].array_count = 1;
        s_sh_raymarch = sg_make_shader(&rs);
        pd.shader = s_sh_raymarch; pd.label = "fxtk-raymarch";
        s_pip_raymarch = sg_make_pipeline(&pd);
    }

    /* 全屏 blit 用的静态四边形 (NDC + UV) */
    {
        static const vtx_t q[4] = {
            { -1.0f,  1.0f, 0.0f, 0.0f, 0xFFFFFFFFu },
            {  1.0f,  1.0f, 1.0f, 0.0f, 0xFFFFFFFFu },
            {  1.0f, -1.0f, 1.0f, 1.0f, 0xFFFFFFFFu },
            { -1.0f, -1.0f, 0.0f, 1.0f, 0xFFFFFFFFu },
        };
        static const uint32_t qi[6] = { 0, 1, 2, 0, 2, 3 };
        s_blit_vb = sg_make_buffer(&(sg_buffer_desc){ .data = SG_RANGE(q), .usage.vertex_buffer = true, .label = "fxtk-blit-vb" });
        s_blit_ib = sg_make_buffer(&(sg_buffer_desc){ .data = SG_RANGE(qi), .usage.index_buffer = true, .label = "fxtk-blit-ib" });
    }
    fb_ensure();
    printf("[sokol] 后端=%d 窗口=%dx%d\n", (int)sg_query_backend(), s_w, s_h);
    return 0;
}

static void drv_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    cur_x0 = x0; cur_y0 = y0; cur_w = (uint16_t)(x1 - x0 + 1); cur_idx = 0;
}

static void drv_push_pixels(const uint32_t *px, uint32_t n)
{
    fb_ensure();
    if (!s_fb || !cur_w) return;
    for (uint32_t i = 0; i < n; i++) {
        int x = cur_x0 + (int)(cur_idx % cur_w);
        int y = cur_y0 + (int)(cur_idx / cur_w);
        cur_idx++;
        if (x < 0 || y < 0 || x >= s_fb_w || y >= s_fb_h) continue;
        s_fb[(size_t)y * s_fb_w + x] = 0xFF000000u | (px[i] & 0xFFFFFFu);
        if (!s_dirty) { s_dx0 = s_dx1 = x; s_dy0 = s_dy1 = y; s_dirty = 1; }
        else {
            if (x < s_dx0) s_dx0 = x;
            if (x > s_dx1) s_dx1 = x;
            if (y < s_dy0) s_dy0 = y;
            if (y > s_dy1) s_dy1 = y;
            if (s_dx1 >= s_fb_w) s_dx1 = s_fb_w - 1;
            if (s_dy1 >= s_fb_h) s_dy1 = s_fb_h - 1;
        }
    }
}

static void drv_hold_begin(void) {}
static void drv_hold_end(void) {}

static void drv_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t c)
{
    if (x0 > x1 || y0 > y1) return;
    emit_quad(0, -1, (float)x0, (float)y0, (float)(x1 + 1), (float)(y1 + 1),
              0, 0, 0, 0, 0xFF000000u | (c & 0xFFFFFFu));
}

static void drv_draw_line(int x1, int y1, int x2, int y2, uint32_t c)
{
    /* 粗线→四边形: 计算法向偏移 0.5px */
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) len = 1.0f;
    float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
    uint32_t col = 0xFF000000u | (c & 0xFFFFFFu);
    float ax = x1 + nx, ay = y1 + ny, bx = x2 + nx, by = y2 + ny;
    float cx = x2 - nx, cy = y2 - ny, ex = x1 - nx, ey = y1 - ny;
    if (cmd_new(0, -1) < 0) return;
    if (s_vb_n + 4 > VB_MAX || s_ib_n + 6 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    vtx_push(ax, ay, 0, 0, col); vtx_push(bx, by, 0, 0, col);
    vtx_push(cx, cy, 0, 0, col); vtx_push(ex, ey, 0, 0, col);
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 2; s_ib[s_ib_n++] = b + 3;
    cm->count += 6;
}

static void drv_fill_tri(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t c)
{
    uint32_t col = 0xFF000000u | (c & 0xFFFFFFu);
    if (cmd_new(0, -1) < 0) return;
    if (s_vb_n + 3 > VB_MAX || s_ib_n + 3 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    vtx_push((float)x1, (float)y1, 0, 0, col);
    vtx_push((float)x2, (float)y2, 0, 0, col);
    vtx_push((float)x3, (float)y3, 0, 0, col);
    s_ib[s_ib_n++] = b; s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    cm->count += 3;
}

/* 纹理 blit (文字/离屏画布): tex 指向文本层或本驱动创建的 fxtk_sokol_tex_t */
/* v2.4 档位 2: 羽化线段。把线段扩成四边形, 局部坐标沿线段轴向 → 片元用胶囊距离羽化边缘。
 * 相比双三角硬边: 任意角度都平滑(斜线不再有阶梯)。 */
static void drv_draw_line_aa(int x1, int y1, int x2, int y2, int w, uint32_t c)
{
    if (w < 1) w = 1;
    float hw = (float)w * 0.5f;
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {          /* 退化成一个点 → 用圆点(胶囊半径) */
        emit_round(x1 - w / 2, y1 - w / 2, x1 + w / 2, y1 + w / 2, w / 2, 0, 0xFF000000u | (c & 0xFFFFFFu));
        return;
    }
    float ux = dx / len, uy = dy / len;      /* 轴向 */
    float px = -uy, py = ux;                 /* 法向 */
    float hl = len * 0.5f;                   /* 半长 */
    float pad = hw + 1.0f;                   /* 留 1px 给羽化, 免得被几何切掉 */
    float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
    float ex = ux * (hl + pad), ey = uy * (hl + pad);   /* 轴向端点(含端帽空间) */
    float nx = px * pad, ny = py * pad;                 /* 法向半宽空间 */
    if (cmd_new(3, -1) < 0) return;
    if (s_vb_n + 4 > VB_MAX || s_ib_n + 6 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    float lx = hl + pad, ly = pad;           /* 局部坐标半宽(片元里用) */
    uint32_t col = 0xFF000000u | (c & 0xFFFFFFu);
    /* 四角: (±ex±nx, ±ey±ny); 局部 uv 与之一一对应 */
    vtx_push_sdf(cx - ex - nx, cy - ey - ny, -lx, -ly, col, hl, hw, -1.0f, 0.0f);
    vtx_push_sdf(cx + ex - nx, cy + ey - ny,  lx, -ly, col, hl, hw, -1.0f, 0.0f);
    vtx_push_sdf(cx + ex + nx, cy + ey + ny,  lx,  ly, col, hl, hw, -1.0f, 0.0f);
    vtx_push_sdf(cx - ex + nx, cy - ey + ny, -lx,  ly, col, hl, hw, -1.0f, 0.0f);
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 2; s_ib[s_ib_n++] = b + 3;
    cm->count += 6;
}

/* v2.4: GPU SDF 抗锯齿钩子 (框架的 fx_fill_rect_round / fx_draw_rect_round 在档位≥1 时走这里) */
static void drv_fill_rect_round(int x1, int y1, int x2, int y2, int r, uint32_t c)
{
    px_flush();                                  /* 保持与像素层的 z 序 */
    emit_round(x1, y1, x2, y2, r, 0, 0xFF000000u | (c & 0xFFFFFFu));
}
static void drv_stroke_rect_round(int x1, int y1, int x2, int y2, int r, int bw, uint32_t c)
{
    px_flush();
    if (bw < 1) bw = 1;
    emit_round(x1, y1, x2, y2, r, bw, 0xFF000000u | (c & 0xFFFFFFu));
}

static void drv_blit_tex(void *tex, int sx, int sy, int sw, int sh, int dx, int dy)
{
    fxtk_sokol_tex_t *t = (fxtk_sokol_tex_t *)tex;
    if (!t) return;
    px_flush();     /* 保持顺序: 先落像素层 */
    int slot = tex_alloc(t->img, t->view, t->w, t->h);
    if (slot < 0) return;
    float u0 = (float)sx / (float)t->w, v0 = (float)sy / (float)t->h;
    float u1 = (float)(sx + sw) / (float)t->w, v1 = (float)(sy + sh) / (float)t->h;
    emit_quad(1, slot, (float)dx, (float)dy, (float)(dx + sw), (float)(dy + sh), u0, v0, u1, v1, 0xFFFFFFFFu);
}

/* 一帧内的上传缓存: 同一张源图(指针相同)只上传一次, 之后复用同一纹理槽。
 * 为什么必须有: 图片池每帧只允许每张图更新一次(sokol 校验), 而一个伪 3D 场景一帧要画上百个
 * 四边形 —— 它们其实只有 3~4 张不同的瓦片图。没有这层去重, 池子第 8 张之后的上传全被丢掉,
 * 表现就是"画面全空但 HUD 显示画了 94 个四边形"(实测踩到)。 */
#define IMGC_MAX 32
static struct { const uint32_t *px; int w, h, dark, slot; } s_imgc[IMGC_MAX];
static int s_imgc_n = 0;

/* 把一张图上传到图片池的下一张, 返回可用纹理槽(失败 -1)。只上传不绘制 —— blit 与四边形形变共用。 */
static int img_upload(const uint32_t *px, int w, int h, int dark)
{
    for (int i = 0; i < s_imgc_n; i++)
        if (s_imgc[i].px == px && s_imgc[i].w == w && s_imgc[i].h == h && s_imgc[i].dark == dark)
            return s_imgc[i].slot;
    int k = s_img_slot++;
    if (k < IMG_SLOTS) {
        s_img_used = k + 1;
        if (s_img_used > s_img_used_max) s_img_used_max = s_img_used;
    }
    if (k >= IMG_SLOTS) {
        /* 池用尽: 宁可跳过这一张, 也不能对同一张图更新两次(会 abort 整个进程) */
        if (s_img_over++ == 0) fx_log(FX_LOG_WARN, "[sokol] 单帧图片数超过 %d, 已跳过多余的图片", IMG_SLOTS);
        return -1;
    }
    if (s_img_w[k] != w || s_img_h[k] != h || s_img_tex[k].id == SG_INVALID_ID) {
        if (s_img_tex[k].id != SG_INVALID_ID) { sg_destroy_view(s_img_view[k]); sg_destroy_image(s_img_tex[k]); }
        s_img_tex[k] = sg_make_image(&(sg_image_desc){
            .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8,
            .usage.dynamic_update = true, .label = "fxtk-image" });
        s_img_view[k] = sg_make_view(&(sg_view_desc){ .texture = { .image = s_img_tex[k] }, .label = "fxtk-image-view" });
        s_img_w[k] = w; s_img_h[k] = h;
        if (s_img_cap < w * h) { free(s_img_stage); s_img_stage = (uint32_t *)malloc((size_t)w * h * 4); s_img_cap = w * h; }
    }
    if (!s_img_stage) return -1;
    for (int i = 0; i < w * h; i++) {
        uint32_t c = px[i] & 0xFFFFFFu;
        if (dark) c = ((c >> 1) & 0x7F7F7Fu);
        s_img_stage[i] = rgba_pack(c);
    }
    sg_update_image(s_img_tex[k], &(sg_image_data){ .mip_levels[0] = { s_img_stage, (size_t)w * h * 4 } });
    int slot = tex_alloc(s_img_tex[k], s_img_view[k], w, h);
    if (slot >= 0 && s_imgc_n < IMGC_MAX) {
        s_imgc[s_imgc_n].px = px; s_imgc[s_imgc_n].w = w; s_imgc[s_imgc_n].h = h;
        s_imgc[s_imgc_n].dark = dark; s_imgc[s_imgc_n].slot = slot; s_imgc_n++;
    }
    return slot;
}

/* 图片 blit: 取一张空闲图片上传后按贴图绘制 (见上面 img pool 注释) */
static void drv_blit_img(const uint32_t *px, int w, int h, int dx, int dy, int dw, int dh, int dark)
{
    if (!px || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    px_flush();
    int slot = img_upload(px, w, h, dark);
    if (slot < 0) return;
    emit_quad(1, slot, (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh), 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

/* v2.4 P4: GPU 真透视四边形形变。每角权重 d_i 由 fx_quad_corner_weights 从单应算出,
 * 写进顶点 sdf0.x → 由 gl_Position.w 承载, 硬件做透视校正插值。
 * 退化/自交四边形回退成包围盒直绘(与框架 CPU 路径的回退语义一致)。 */
static void drv_draw_image_quad(const uint32_t *px, int w, int h, const float *xy8, int bilinear)
{
    (void)bilinear;
    if (!px || w <= 0 || h <= 0 || !xy8) return;
    px_flush();
    float d4[4];
    if (!fx_quad_corner_weights(xy8, d4)) {
        float minx = xy8[0], maxx = xy8[0], miny = xy8[1], maxy = xy8[1];
        for (int i = 1; i < 4; i++) {
            if (xy8[i * 2]     < minx) minx = xy8[i * 2];
            if (xy8[i * 2]     > maxx) maxx = xy8[i * 2];
            if (xy8[i * 2 + 1] < miny) miny = xy8[i * 2 + 1];
            if (xy8[i * 2 + 1] > maxy) maxy = xy8[i * 2 + 1];
        }
        drv_blit_img(px, w, h, (int)minx, (int)miny, (int)(maxx - minx + 1), (int)(maxy - miny + 1), 0);
        return;
    }
    int slot = img_upload(px, w, h, 0);
    if (slot < 0) return;
    if (cmd_new(4, slot) < 0) return;
    if (s_vb_n + 4 > VB_MAX || s_ib_n + 6 > IB_MAX) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    uint32_t b = (uint32_t)s_vb_n;
    const uint32_t col = 0xFFFFFFFFu;
    vtx_push_sdf(xy8[0], xy8[1], 0.0f, 0.0f, col, d4[0], 0.0f, 0.0f, 0.0f);
    vtx_push_sdf(xy8[2], xy8[3], 1.0f, 0.0f, col, d4[1], 0.0f, 0.0f, 0.0f);
    vtx_push_sdf(xy8[4], xy8[5], 1.0f, 1.0f, col, d4[2], 0.0f, 0.0f, 0.0f);
    vtx_push_sdf(xy8[6], xy8[7], 0.0f, 1.0f, col, d4[3], 0.0f, 0.0f, 0.0f);
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 1; s_ib[s_ib_n++] = b + 2;
    s_ib[s_ib_n++] = b;     s_ib[s_ib_n++] = b + 2; s_ib[s_ib_n++] = b + 3;
    cm->count += 6;
}

static int drv_touch_read(int *x, int *y, int *pressed)
{
    if (s_qt_h == s_qt_t) return 0;
    *x = s_q_touch[s_qt_h].x; *y = s_q_touch[s_qt_h].y; *pressed = s_q_touch[s_qt_h].p;
    {   /* ①输入排队延迟: 入队到框架取走 */
        uint32_t lat = now_us() - s_q_touch[s_qt_h].t;
        { int d = (s_qt_t - s_qt_h + EVQ_MAX) % EVQ_MAX; if (d > s_depth_max) s_depth_max = d; }
        if (lat > s_lat_us_max) s_lat_us_max = lat;
        s_lat_us_sum += lat; s_lat_n++;
    }
    s_qt_h = (s_qt_h + 1) % EVQ_MAX;
    return 1;
}

static int drv_key_read(fx_keyev_t *ev)
{
    if (s_qk_h == s_qk_t) return 0;
    *ev = s_q_key[s_qk_h];
    s_qk_h = (s_qk_h + 1) % EVQ_MAX;
    return 1;
}

static int drv_wheel_read(int *x, int *y, int *dy)
{
    if (s_qw_h == s_qw_t) return 0;
    *x = s_q_wheel[s_qw_h].x; *y = s_q_wheel[s_qw_h].y; *dy = s_q_wheel[s_qw_h].dy;
    s_qw_h = (s_qw_h + 1) % EVQ_MAX;
    return 1;
}

static void drv_set_title(const char *s) { sapp_set_window_title(s ? s : "fxtk"); }
static void drv_set_clip_rect(int x1, int y1, int x2, int y2)
{
    s_clip_x1 = x1; s_clip_y1 = y1; s_clip_x2 = x2; s_clip_y2 = y2;
}
static void drv_clip_set(const char *s) { (void)s; }
static const char *drv_clip_get(void) { return ""; }
/* v2.4 P4: 旋转贴图 —— 之前是空实现, 于是图形页那两张旋转图片在 sokol 后端整块消失
 * (与 SDL 版一眼可见的差别)。现在直接用 P4 的 GPU 真透视四边形: 旋转是仿射特例(权重恒 1),
 * 硬件自己做旋转 + 双线性。语义与 SDL 驱动严格对齐: 以 (cx,cy) 为中心、尺寸 dw x dh、角度取 -ang 度
 * (SDL_RenderCopyEx 正角为顺时针, 框架传的是逆时针角)。 */
static void drv_draw_image_quad(const uint32_t *px, int w, int h, const float *xy8, int bilinear);
static void drv_blit_img_rot(const uint32_t *px, int w, int h, int cx, int cy, int dw, int dh, double ang)
{
    if (!px || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    px_flush();
    const double th = -ang * 3.14159265358979323846 / 180.0;
    const double c = cos(th), s = sin(th);
    static const float ox[4] = { -1, 1, 1, -1 }, oy[4] = { -1, -1, 1, 1 };
    float xy8[8];
    for (int i = 0; i < 4; i++) {
        double lx = ox[i] * (dw * 0.5), ly = oy[i] * (dh * 0.5);
        xy8[i * 2]     = (float)(cx + lx * c - ly * s);
        xy8[i * 2 + 1] = (float)(cy + lx * s + ly * c);
    }
    drv_draw_image_quad(px, w, h, xy8, 1);
}

#if !defined(_WIN32)
#include <GL/gl.h>
#endif

/* 截图: 必须在 pass 内、交换缓冲【之前】回读 —— 交换后再读默认帧缓冲只会得到空白。
 * 因此由 main 在需要的帧上"请求", 本驱动在 sg_commit() 前抓取并暂存。 */
static uint32_t *s_shot_buf = NULL;
static int s_shot_w = 0, s_shot_h = 0, s_shot_cap = 0, s_shot_req = 0, s_shot_ready = 0;

/* v2.4: GPU 光追 —— 作为一条绘制命令插入命令流(不占 CPU 像素缓冲, 不做回读):
 * 视口限定到目标矩形, 全屏四边形(fs 用 gl_FragCoord 反推局部像素坐标) */
void fxtk_sokol_raymarch(float time, int x1, int y1, int x2, int y2)
{
    if (x2 < x1 || y2 < y1) return;
    px_flush();                                   /* 保持 z 序 */
    if (cmd_new(2, -1) < 0) return;
    cmd_t *cm = &s_cmd[s_cmd_n - 1];
    cm->count = 6;                                /* 复用整屏 blit 四边形 */
    cm->time = time;
    cm->cx1 = x1; cm->cy1 = y1; cm->cx2 = x2; cm->cy2 = y2;   /* 顺带用裁剪矩形记录目标区 */
}

/* 测试用: 直接往触摸队列注入一次按下+抬起 (验证输入链路, 不依赖真实鼠标) */
void fxtk_sokol_inject_click(int x, int y)
{
    q_push_touch(x, y, 1);
    q_push_touch(x, y, 0);
}

/* 测试用: 注入一次拖拽 (按下 → 中间移动若干步 → 抬起), 用于自动化验证拖动/残留 */
void fxtk_sokol_inject_drag(int x0, int y0, int x1, int y1, int steps)
{
    q_push_touch_edge(x0, y0, 1);                  /* 按下是边沿, 不能被后续移动合并掉 */
    for (int i = 1; i <= steps; i++)
        q_push_touch(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 1);
    q_push_touch_edge(x1, y1, 0);
}

/* --- 持续拖动发生器 (FXTK_DRAG_LOOP="x0,y0,x1,y1[,steps[,每帧事件数]]") ---
 * 为什么需要它: 一次性灌 24 个事件测不出"跟手不跟手"。真实鼠标按系统速率(常见 125~1000Hz)
 * 投递移动事件, 所以要让"每帧到达 N 个移动事件"持续发生, 再看框架消费到的是不是最新位置。 */
static int s_dl_on = 0, s_dl_x0, s_dl_y0, s_dl_x1, s_dl_y1, s_dl_steps = 24, s_dl_i = 0, s_dl_per = 3;
void fxtk_sokol_drag_loop_start(int x0, int y0, int x1, int y1, int steps, int per_frame)
{
    s_dl_on = 1; s_dl_x0 = x0; s_dl_y0 = y0; s_dl_x1 = x1; s_dl_y1 = y1;
    s_dl_steps = steps > 1 ? steps : 2; s_dl_per = per_frame > 0 ? per_frame : 1; s_dl_i = 0;
    q_push_touch_edge(x0, y0, 1);
}
void fxtk_sokol_drag_loop_tick(void)
{
    if (!s_dl_on) return;
    for (int k = 0; k < s_dl_per; k++) {
        int ph = s_dl_i % (s_dl_steps * 2);                 /* 往返三角波, 拖到终点再拖回来 */
        int t  = ph < s_dl_steps ? ph : (s_dl_steps * 2 - ph);
        q_push_touch(s_dl_x0 + (s_dl_x1 - s_dl_x0) * t / s_dl_steps,
                     s_dl_y0 + (s_dl_y1 - s_dl_y0) * t / s_dl_steps, 1);
        s_dl_i++;
    }
}

void fxtk_sokol_request_shot(void) { s_shot_req = 1; s_shot_ready = 0; }

static void shot_capture(void)
{
    if (!s_shot_req) return;
    s_shot_req = 0;
    int w = s_w, h = s_h;
    if (w <= 0 || h <= 0) return;
    if (s_shot_cap < w * h) { free(s_shot_buf); s_shot_buf = (uint32_t *)malloc((size_t)w * h * 4); s_shot_cap = w * h; }
    if (!s_shot_buf) return;
#if !defined(_WIN32)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s_shot_buf);
    /* 框架的 fx_screenshot 约定 read_pixels 交出的是 0xAARRGGBB(与 SDL 驱动一致)。
     * glReadPixels(GL_RGBA) 在小端下得到的是 A,B,G,R 布局的字, 必须搬一次字节 ——
     * 少了这一步, 回读会【和渲染的 R/B 错误互相抵消】, 截出来的图看着是对的, 屏幕却是错的:
     * 正是这个坑让我上一轮误判"颜色已修好"。 */
    for (int i = 0; i < w * h; i++) s_shot_buf[i] = rgba_pack(s_shot_buf[i] & 0xFFFFFFu);
    for (int y = 0; y < h / 2; y++)          /* GL 原点在左下 → 翻成左上 */
        for (int x = 0; x < w; x++) {
            uint32_t t = s_shot_buf[(size_t)y * w + x];
            s_shot_buf[(size_t)y * w + x] = s_shot_buf[(size_t)(h - 1 - y) * w + x];
            s_shot_buf[(size_t)(h - 1 - y) * w + x] = t;
        }
    s_shot_w = w; s_shot_h = h; s_shot_ready = 1;
#endif
}

static int drv_read_pixels(uint32_t *dst, int w, int h)
{
    if (!dst || !s_shot_ready || w != s_shot_w || h != s_shot_h) return 0;
    memcpy(dst, s_shot_buf, (size_t)w * h * 4);
    s_shot_ready = 0;
    return 1;
}

fx_driver_t fx_sokol_driver = {
    .width = 480, .height = 272,
    .init = drv_init,
    .set_window = drv_set_window,
    .push_pixels = drv_push_pixels,
    .hold_begin = drv_hold_begin,
    .hold_end = drv_hold_end,
    .fill_rect = drv_fill_rect,
    .touch_read = drv_touch_read,
    .key_read = drv_key_read,
    .clip_set = drv_clip_set,
    .clip_get = drv_clip_get,
    .set_title = drv_set_title,
    .wheel_read = drv_wheel_read,
    .set_clip_rect = drv_set_clip_rect,
    .blit_img = drv_blit_img,
    .blit_tex = drv_blit_tex,
    .blit_img_rot = drv_blit_img_rot,
    .fill_tri = drv_fill_tri,
    .draw_line = drv_draw_line,
    .read_pixels = drv_read_pixels,
    .raymarch = fxtk_sokol_raymarch,   /* v2.4: GPU 实时光线步进 */
    .fill_rect_round = drv_fill_rect_round,     /* v2.4: GPU SDF 圆角矩形 (抗锯齿) */
    .stroke_rect_round = drv_stroke_rect_round, /* v2.4: GPU SDF 圆角描边 (抗锯齿) */
    .draw_line_aa = drv_draw_line_aa,           /* v2.4 档位 2: GPU 羽化线段 */
    .draw_image_quad = drv_draw_image_quad,     /* v2.4 P4: GPU 真透视四边形形变 */
};

/* ================= 帧的呈现 (由 sokol_app 回调触发) ================= */

static void canvas_ensure(void)
{
    if (s_canvas_img.id != SG_INVALID_ID && s_canvas_w == s_w && s_canvas_h == s_h) return;
    if (s_canvas_img.id != SG_INVALID_ID) {
        sg_destroy_view(s_canvas_attach); sg_destroy_view(s_canvas_tex); sg_destroy_image(s_canvas_img);
    }
    s_canvas_img = sg_make_image(&(sg_image_desc){
        .width = s_w, .height = s_h, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.color_attachment = true, .label = "fxtk-canvas" });
    s_canvas_attach = sg_make_view(&(sg_view_desc){ .color_attachment = { .image = s_canvas_img }, .label = "fxtk-canvas-attach" });
    s_canvas_tex = sg_make_view(&(sg_view_desc){ .texture = { .image = s_canvas_img }, .label = "fxtk-canvas-tex" });
    s_canvas_w = s_w; s_canvas_h = s_h;
    s_need_clear = 1;
}

void fxtk_sokol_frame(int fb_w, int fb_h)
{
    uint32_t t_sub0 = now_us();
    /* 窗口尺寸变化统一走 apply_size(它会重建缓冲并让框架重排/重绘)。
     * 早期实现在这里直接赋值 s_w/s_h, 会与按旧尺寸分配的 fb 脱节 → 堆越界崩溃。 */
    if (fb_w != s_w || fb_h != s_h) fxtk_sokol_apply_size(fb_w, fb_h);
    {   /* FPS 统计 (与 SDL 驱动同款语义: 每秒刷新一次) */
        static uint32_t t0 = 0;
        uint32_t now = (uint32_t)fx_time_ms();
        if (!t0) t0 = now;
        s_fps_n++;
        if (now - t0 >= 1000) { s_fps = s_fps_n; s_fps_n = 0; t0 = now; }
    }
    px_flush();
    /* 自检 B: FXTK_SOKOL_TEST=1 时追加红/绿两个实心块 (必须在 update 之前) */
    if (getenv("FXTK_SOKOL_TEST")) {
        emit_quad(0, -1, 40, 40, 240, 190, 0, 0, 0, 0, 0xFFFF0000u);
        emit_quad(0, -1, 300, 300, 500, 450, 0, 0, 0, 0, 0xFF00FF00u);
    }
    if (s_vb_n > 0) sg_update_buffer(s_vbuf, &(sg_range){ s_vb, (size_t)s_vb_n * sizeof(vtx_t) });
    if (s_ib_n > 0) sg_update_buffer(s_ibuf, &(sg_range){ s_ib, (size_t)s_ib_n * sizeof(uint32_t) });

    canvas_ensure();
    /* --- 第 1 趟: 画进持久画布纹理 (LOAD 保留上一帧的静态内容) --- */
    sg_begin_pass(&(sg_pass){
        .action = { .colors[0] = {
            .load_action = s_need_clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD,
            .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f } } },
        .attachments = { .colors[0] = s_canvas_attach } });
    s_need_clear = 0;

    for (int i = 0; i < s_cmd_n; i++) {
        cmd_t *c = &s_cmd[i];
        if (c->count <= 0) continue;
        {   /* 裁剪: 与屏幕求交后设 scissor (origin_top_left) */
            int cx1 = c->cx1 < 0 ? 0 : c->cx1;
            int cy1 = c->cy1 < 0 ? 0 : c->cy1;
            int cx2 = c->cx2 > s_w - 1 ? s_w - 1 : c->cx2;
            int cy2 = c->cy2 > s_h - 1 ? s_h - 1 : c->cy2;
            if (cx2 < cx1 || cy2 < cy1) continue;
            /* 【v2.4 关键修正】裁剪矩形是"屏幕坐标(左上原点)", 而本 pass 是【离屏 render target】:
             * sokol 对 render target pass 的原点是左下(GL 约定), 只有 swapchain pass 才是左上。
             * 之前这里传 origin_top_left=true → 裁剪框在垂直方向被镜像, 于是"控件自己的紧裁剪"
             * 把该控件整块裁掉(表现为: 圆角按钮填充全部消失, SDF 抗锯齿无效)。
             * 画布裁剪之所以"看起来没问题", 只是因为画布矩形接近满屏、镜像后仍大面积重合。
             * 与光追分支里的 u[1] = s_h-(y+h) 是同一套换算是同一件事。 */
            /* 【实测结论, 别再改】origin_top_left 必须传 false + 直接用屏幕坐标的 y。
             * render-target pass 里 sokol 不做 y 翻转(与它在 swapchain pass 上的行为不同),
             * 传 true 会让裁剪框垂直镜像 → 控件用自己的紧裁剪时被整块裁掉(圆角填充全消失)。
             * 四种组合实测: (x,y,true)=0px, (x,y,false)=8784px 全中, (x,H-y-h,false)=0px,
             * (x,H-y-h,true)=8784px 全中 —— 等价的两组正好互为镜像, 选直接写法。 */
            sg_apply_scissor_rect(cx1, cy1, cx2 - cx1 + 1, cy2 - cy1 + 1, false);
        }
        if (c->pip == 2) {
            /* 光追: 视口=目标矩形, 用整屏 blit 四边形(NDC), fs 依据 u_rect 反推局部像素 */
            int w = c->cx2 - c->cx1 + 1, h = c->cy2 - c->cy1 + 1;
            float u[5];
            u[0] = (float)c->cx1;
            u[1] = (float)(s_h - (c->cy1 + h));    /* 转成帧缓冲左下原点 */
            u[2] = (float)w; u[3] = (float)h;
            u[4] = c->time;
            sg_apply_pipeline(s_pip_raymarch);
            sg_apply_uniforms(0, &(sg_range){ u, sizeof(u) });
            sg_apply_viewport(c->cx1, c->cy1, w, h, true);
            sg_bindings rb = {0};
            rb.vertex_buffers[0] = s_blit_vb;
            rb.index_buffer = s_blit_ib;
            sg_apply_bindings(&rb);
            sg_draw(0, 6, 1);
            sg_apply_viewport(0, 0, s_w, s_h, true);   /* 复原, 免得影响后续命令 */
            continue;
        }
        sg_apply_pipeline(c->pip == 0 ? s_pip_solid :
                          (c->pip == 3 ? s_pip_sdf : (c->pip == 4 ? s_pip_quad : s_pip_tex)));
        sg_bindings b = {0};
        b.vertex_buffers[0] = s_vbuf;
        b.index_buffer = s_ibuf;
        if ((c->pip == 1 || c->pip == 4) && c->tex >= 0) {
            b.views[0] = s_tex[c->tex].view;
            b.samplers[0] = s_tex[c->tex].smp;
        }
        sg_apply_bindings(&b);
        sg_draw(c->first, c->count, 1);   /* 索引绘制: base_element = 索引偏移 ✓ */
    }
    sg_end_pass();

    /* --- 第 2 趟: 把画布整屏贴到交换链 (交换链每帧覆盖, 用 CLEAR) --- */
    sg_begin_pass(&(sg_pass){
        .action = { .colors[0] = { .load_action = SG_LOADACTION_CLEAR,
                                   .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f } } },
        .swapchain = sglue_swapchain() });
    sg_apply_scissor_rect(0, 0, s_w, s_h, true);   /* 整屏 blit 不受之前命令的裁剪影响 */
    sg_apply_pipeline(s_pip_tex);
    sg_bindings bb = {0};
    bb.vertex_buffers[0] = s_blit_vb;
    bb.index_buffer = s_blit_ib;
    bb.views[0] = s_canvas_tex;
    bb.samplers[0] = s_smp_nearest;
    sg_apply_bindings(&bb);
    sg_draw(0, 6, 1);
    sg_end_pass();
    shot_capture();      /* 交换前回读 (交换后默认帧缓冲内容不可依赖) */
    sg_commit();

    { uint32_t d = now_us() - t_sub0; if (d > s_sub_us_max) s_sub_us_max = d; s_sub_us_sum += d; }
    if (getenv("FXTK_STAT")) {
        static int n = 0;
        if ((n++ % 30) == 0) {
            uint32_t t = now_us();
            if (s_stat_t0 == 0) s_stat_t0 = t;
            if (t - s_stat_t0 >= 1000000u) {           /* 每秒汇总一次 */
                double sec = (t - s_stat_t0) / 1000000.0;
                fprintf(stderr,
                    "[stat] %dx%d fps=%d 控件=%d | 顶点=%d 命令=%d 纹理槽=%d 文本纹理=%d/淘汰%d | "
                    "排队 max=%.1fms avg=%.2fms(%d) 合并=%ld 深度max=%d | 整帧 max=%.1fms avg=%.2fms | "
                    "像素 max=%.1fms 面积=%.0fpx/帧 次数=%.1f/帧 | 提交 max=%.1fms avg=%.2fms | 图片/帧 max=%d 溢出=%d\n",
                    s_w, s_h, s_fps, fxtk_widget_count(), s_vb_n, s_cmd_n, s_tex_n,
                    fxtk_text_created, fxtk_text_evicted,
                    s_lat_us_max / 1000.0, s_lat_n ? s_lat_us_sum / 1000.0 / s_lat_n : 0.0, s_lat_n, s_merge_n, s_depth_max,
                    s_frame_us_max / 1000.0, s_frame_us_sum / 1000.0 / sec,
                    s_px_us_max / 1000.0, s_px_area / sec, s_px_calls / sec,
                    s_sub_us_max / 1000.0, s_sub_us_sum / 1000.0 / sec, s_img_used_max, s_img_over,
                    s_sdf_max, fx_widget_aa_level());
                s_stat_t0 = t;
                s_lat_us_max = s_lat_us_sum = 0; s_lat_n = 0; s_merge_n = 0; s_depth_max = 0;
                s_frame_us_max = 0; s_frame_us_sum = 0;
                s_px_us_max = 0; s_px_area = 0; s_px_calls = 0;
                s_sub_us_max = 0; s_sub_us_sum = 0; s_img_used_max = 0; s_sdf_max = 0;
            }
        }
    }
    if (s_sdf_n > s_sdf_max) s_sdf_max = s_sdf_n;

    s_vb_n = 0; s_ib_n = 0; s_cmd_n = 0; s_tex_n = 0;
    s_img_slot = 0; s_img_used = 0; s_imgc_n = 0;
    s_sdf_n = 0;
}

/* ================= SDL 驱动同款辅助 API (框架/演示依赖) ================= */

static int s_shift = 0, s_rc = 0, s_rc_x = 0, s_rc_y = 0;
static int g_uicap = 160;

int fxtk_shift_down(void) { return s_shift; }
int fxtk_right_click(int *x, int *y) { if (!s_rc) return 0; s_rc = 0; *x = s_rc_x; *y = s_rc_y; return 1; }
int fxtk_fps(void) { return s_fps; }
int fxtk_ui_scale(void)
{
    extern int fxtk_drv_width(void);
    int w = fxtk_drv_width();
    int sc = w > 0 ? (w * 100) / 480 : 100;
    if (sc < 100) sc = 100;
    if (!getenv("FXTK_COMPACT") && sc > g_uicap) sc = g_uicap;
    return sc;
}
void fxtk_drv_set_uicap(int p) { if (p >= 100) g_uicap = p; }

/* sokol_app 侧需要拿到这些状态 */
int  fxtk_sokol_tex_count(void) { return s_tex_n; }
int  fxtk_sokol_vtx_count(void) { return s_vb_n; }

/* 尺寸变化: 与 SDL 驱动同构 —— 丢缓冲、重建纹理、**让框架重排并全量重绘**。
 * 漏掉 fx_layout/fx_repaint 会让新暴露的区域一直没人画 → 缩放后几乎全屏黑(实测踩坑)。 */
void fxtk_sokol_apply_size(int w, int h)
{
    s_clip_x1 = 0; s_clip_y1 = 0; s_clip_x2 = 32767; s_clip_y2 = 32767;
    if (w < 160) w = 160;
    if (h < 120) h = 120;
    if (w == s_w && h == s_h) return;
    s_w = w; s_h = h;
    fx_sokol_driver.width = (uint32_t)s_w;
    fx_sokol_driver.height = (uint32_t)s_h;
    free(s_fb); s_fb = NULL; s_fb_w = s_fb_h = 0; s_dirty = 0;
    fb_ensure();
    s_need_clear = 1;          /* 离屏画布下一帧会按新尺寸重建并清一次 */
    fx_layout();
    fx_repaint();
    fx_repaint();
}

/* ================= sokol_app 事件 → 框架轮询队列 =================
 * 语义与 SDL 驱动对齐: TEXTINPUT/CHAR → key=0 的文本事件; 特殊键 → fx_key* 常量;
 * Ctrl+C/V/X/A → utf8[0]=字母 + mod; mod 位: 1=Ctrl, 2=Shift。 */
void fxtk_sokol_handle_event(const sapp_event *e)
{
    static int s_mouse_down = 0;
    fx_keyev_t k;
    memset(&k, 0, sizeof(k));
    switch (e->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) { s_rc = 1; s_rc_x = (int)e->mouse_x; s_rc_y = (int)e->mouse_y; break; }
        s_mouse_down = 1; q_push_touch_edge((int)e->mouse_x, (int)e->mouse_y, 1); break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        s_mouse_down = 0; q_push_touch_edge((int)e->mouse_x, (int)e->mouse_y, 0); break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        q_push_touch((int)e->mouse_x, (int)e->mouse_y, s_mouse_down); break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        q_push_wheel((int)e->mouse_x, (int)e->mouse_y, (int)(e->scroll_y * 40.0f)); break;
    case SAPP_EVENTTYPE_TOUCHES_BEGAN:
    case SAPP_EVENTTYPE_TOUCHES_MOVED:
    case SAPP_EVENTTYPE_TOUCHES_ENDED:
        if (e->num_touches > 0) {
            int moved = (e->type == SAPP_EVENTTYPE_TOUCHES_MOVED);
            if (moved) q_push_touch((int)e->touches[0].pos_x, (int)e->touches[0].pos_y, 1);
            else q_push_touch_edge((int)e->touches[0].pos_x, (int)e->touches[0].pos_y,
                                   e->type != SAPP_EVENTTYPE_TOUCHES_ENDED);
        } else if (e->type == SAPP_EVENTTYPE_TOUCHES_ENDED)
            q_push_touch_edge(0, 0, 0);
        break;
    case SAPP_EVENTTYPE_CHAR: {
        uint32_t cp = e->char_code;
        char *o = k.utf8;
        if (cp < 0x80) { o[0] = (char)cp; o[1] = 0; }
        else if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); o[2] = 0; }
        else if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); o[3] = 0; }
        else { o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); o[4] = 0; }
        k.key = 0; k.down = 1;
        q_push_key(&k);
        break; }
    case SAPP_EVENTTYPE_KEY_DOWN: {
        if (e->key_repeat) break;
        int kc = 0;
        switch (e->key_code) {
        case SAPP_KEYCODE_BACKSPACE: kc = FX_KEY_BACKSPACE; break;
        case SAPP_KEYCODE_ENTER:     kc = FX_KEY_RETURN; break;
        case SAPP_KEYCODE_ESCAPE:    kc = FX_KEY_ESCAPE; break;
        case SAPP_KEYCODE_LEFT:      kc = FX_KEY_LEFT; break;
        case SAPP_KEYCODE_RIGHT:     kc = FX_KEY_RIGHT; break;
        case SAPP_KEYCODE_HOME:      kc = FX_KEY_HOME; break;
        case SAPP_KEYCODE_END:       kc = FX_KEY_END; break;
        case SAPP_KEYCODE_UP:        kc = FX_KEY_UP; break;
        case SAPP_KEYCODE_DOWN:      kc = FX_KEY_DOWN; break;
        case SAPP_KEYCODE_DELETE:    kc = FX_KEY_DELETE; break;
        default: break;
        }
        int mod = ((e->modifiers & SAPP_MODIFIER_CTRL) ? 1 : 0) | ((e->modifiers & SAPP_MODIFIER_SHIFT) ? 2 : 0);
        s_shift = (mod & 2) ? 1 : 0;
        if (kc) { fx_keyev_t ke; memset(&ke, 0, sizeof(ke)); ke.key = kc; ke.down = 1; ke.mod = mod; q_push_key(&ke); }
        else if (mod & 1) {
            char c = 0;
            if (e->key_code == SAPP_KEYCODE_C) c = 'c';
            else if (e->key_code == SAPP_KEYCODE_V) c = 'v';
            else if (e->key_code == SAPP_KEYCODE_X) c = 'x';
            else if (e->key_code == SAPP_KEYCODE_A) c = 'a';
            else if (e->key_code == SAPP_KEYCODE_L) c = 'l';
            if (c) { fx_keyev_t ke; memset(&ke, 0, sizeof(ke)); ke.key = 0; ke.down = 1; ke.mod = mod; ke.utf8[0] = c; q_push_key(&ke); }
        }
        break; }
    case SAPP_EVENTTYPE_RESIZED:
        fxtk_sokol_apply_size(sapp_width(), sapp_height());
        break;
    default: break;
    }
}
