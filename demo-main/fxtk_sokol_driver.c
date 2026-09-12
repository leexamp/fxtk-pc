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
#if defined(_WIN32)
  #define SOKOL_D3D11
#else
  #define SOKOL_GLCORE
#endif

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
#include "fxtk_backends.h"   /* fx_time_ms: 统一计时, 免依赖 sokol_time */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if !defined(_WIN32) && !defined(SOKOL_GLCORE)
  #error "unexpected backend"
#endif

/* ================= 顶点/命令表 ================= */

typedef struct { float x, y, u, v; uint32_t rgba; } vtx_t;

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
    int  pip;               /* 0 = 实心, 1 = 贴图 */
    int  tex;               /* 纹理槽位 (-1 = 无) */
} cmd_t;
static cmd_t  s_cmd[CMD_MAX];
static int    s_cmd_n = 0;
static sg_buffer s_ibuf;

/* 每帧的纹理槽位: 记录 sg_image + 尺寸, 用于构造 bindings */
typedef struct { sg_image img; sg_view view; sg_sampler smp; int w, h; } tex_slot_t;
static tex_slot_t s_tex[TEX_MAX];
static int s_tex_n = 0;

/* ================= 驱动状态 ================= */

static int s_w = 480, s_h = 272;
static sg_pipeline s_pip_solid, s_pip_tex;
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

/* 当前 set_window 状态 (软件像素按行推送) */
static uint16_t cur_x0, cur_y0, cur_w; static uint32_t cur_idx = 0;

/* 图片上传用纹理 (blit_img) */
static sg_image s_img_tex; static sg_view s_img_view; static int s_img_w = 0, s_img_h = 0;
static uint32_t *s_img_stage = NULL; static int s_img_cap = 0;

/* ================= 输入事件队列 ================= */

#define EVQ_MAX 256
typedef struct { int type; int x, y, p; int dy; fx_keyev_t key; } evq_t;   /* type: 0=none 1=touch 2=wheel 3=key */
static evq_t s_evq[EVQ_MAX];
static int s_evq_head = 0, s_evq_tail = 0;

static void evq_push(const evq_t *e)
{
    int next = (s_evq_tail + 1) % EVQ_MAX;
    if (next == s_evq_head) return;          /* 满则丢弃最旧(不阻塞) */
    s_evq[s_evq_tail] = *e;
    s_evq_tail = next;
}
static int evq_pop(evq_t *out)
{
    if (s_evq_head == s_evq_tail) return 0;
    *out = s_evq[s_evq_head];
    s_evq_head = (s_evq_head + 1) % EVQ_MAX;
    return 1;
}

/* ================= 顶点追加 ================= */

static int cmd_new(int pip, int tex)
{
    if (s_cmd_n >= CMD_MAX) return -1;
    s_cmd[s_cmd_n].first = s_ib_n;
    s_cmd[s_cmd_n].count = 0;
    s_cmd[s_cmd_n].pip = pip;
    s_cmd[s_cmd_n].tex = tex;
    return s_cmd_n++;
}

/* 像素坐标 → NDC (在 CPU 侧算好, 免掉 uniform block 及其名字查找/420pack 扩展等一整类坑) */
static void vtx_push(float x, float y, float u, float v, uint32_t c)
{
    if (s_vb_n >= VB_MAX) return;
    float rw = (float)(s_w > 0 ? s_w : 1), rh = (float)(s_h > 0 ? s_h : 1);
    s_vb[s_vb_n].x = x / rw * 2.0f - 1.0f;
    s_vb[s_vb_n].y = 1.0f - y / rh * 2.0f;
    s_vb[s_vb_n].u = u; s_vb[s_vb_n].v = v;
    s_vb[s_vb_n].rgba = c;
    s_vb_n++;
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
    if (s_tex_n >= TEX_MAX) return -1;
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

/* 像素层: 每帧【一次】上传到全屏纹理(sokol 规定同一资源每帧只能写一次),
 * 命令流里按脏区插入贴图四边形; UV 直接用屏幕绝对坐标 → 无需回填, z 序天然正确。
 * (P2.3 可用 sg_write_image_transient 的 dst/extent 改成按脏区子矩形上传, 省带宽) */
static void px_ensure_img(void)
{
    if (s_px_img.id != SG_INVALID_ID && s_px_w == s_w && s_px_h == s_h) return;
    if (s_px_img.id != SG_INVALID_ID) { sg_destroy_view(s_px_view); sg_destroy_image(s_px_img); }
    s_px_img = sg_make_image(&(sg_image_desc){
        .width = s_w, .height = s_h, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.dynamic_update = true, .label = "fxtk-pixels" });
    s_px_view = sg_make_view(&(sg_view_desc){ .texture = { .image = s_px_img }, .label = "fxtk-pixels-view" });
    s_px_w = s_w; s_px_h = s_h;
    if (s_px_stage_cap < s_w * s_h) { free(s_px_stage); s_px_stage = (uint32_t *)malloc((size_t)s_w * s_h * 4); s_px_stage_cap = s_w * s_h; }
    s_dirty = 0;
}

static void px_flush(void)
{
    if (!s_dirty || !s_fb || !s_px_stage) { s_dirty = 0; cur_idx = 0; return; }
    int x0 = s_dx0, y0 = s_dy0, x1 = s_dx1, y1 = s_dy1;
    px_ensure_img();
    int slot = tex_alloc(s_px_img, s_px_view, s_w, s_h);
    if (slot >= 0)
        emit_quad(1, slot, (float)x0, (float)y0, (float)(x1 + 1), (float)(y1 + 1),
                  (float)x0 / (float)s_w, (float)y0 / (float)s_h,
                  (float)(x1 + 1) / (float)s_w, (float)(y1 + 1) / (float)s_h, 0xFFFFFFFFu);
    s_dirty = 0; cur_idx = 0;
}

/* 帧开始: 把整屏软件像素一次性转成 RGBA 并上传 (未写过的像素透明) */
static void px_upload(void)
{
    px_ensure_img();
    if (!s_px_stage || !s_fb) return;
    int any = 0;
    for (int i = 0; i < s_w * s_h; i++) {
        uint32_t c = s_fb[i];
        if (c) { s_px_stage[i] = 0xFF000000u | (c & 0xFFFFFFu); any = 1; }
        else s_px_stage[i] = 0u;
    }
    if (any) {
        sg_update_image(s_px_img, &(sg_image_data){ .mip_levels[0] = { s_px_stage, (size_t)s_w * s_h * 4 } });
        memset(s_fb, 0, (size_t)s_w * s_h * 4);
    }
}

/* ================= fx_driver_t 钩子 ================= */

static int drv_init(void)
{
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
    layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    layout.buffers[0].stride = sizeof(vtx_t);

    static const char *vs_src =
        "#version 330\n"
        "layout(location=0) in vec2 position;\n"   /* 已是 NDC */
        "layout(location=1) in vec2 texcoord0;\n"
        "layout(location=2) in vec4 color0;\n"
        "out vec2 uv; out vec4 vcol;\n"
        "void main() { uv = texcoord0; vcol = color0; gl_Position = vec4(position, 0.0, 1.0); }\n";

    sg_shader_desc shd;
    memset(&shd, 0, sizeof(shd));
    shd.vertex_func.source = vs_src;
    shd.vertex_func.entry = "main";
    shd.attrs[0].glsl_name = "position";
    shd.attrs[1].glsl_name = "texcoord0";
    shd.attrs[2].glsl_name = "color0";

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
        if (x < 0 || y < 0 || x >= s_w || y >= s_h) continue;
        s_fb[(size_t)y * s_w + x] = 0xFF000000u | (px[i] & 0xFFFFFFu);
        if (!s_dirty) { s_dx0 = s_dx1 = x; s_dy0 = s_dy1 = y; s_dirty = 1; }
        else {
            if (x < s_dx0) s_dx0 = x;
            if (x > s_dx1) s_dx1 = x;
            if (y < s_dy0) s_dy0 = y;
            if (y > s_dy1) s_dy1 = y;
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

/* 图片 blit: 上传到共享纹理后按贴图绘制 */
static void drv_blit_img(const uint32_t *px, int w, int h, int dx, int dy, int dw, int dh, int dark)
{
    if (!px || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    px_flush();
    if (s_img_w != w || s_img_h != h || s_img_tex.id == SG_INVALID_ID) {
        if (s_img_tex.id != SG_INVALID_ID) { sg_destroy_view(s_img_view); sg_destroy_image(s_img_tex); }
        s_img_tex = sg_make_image(&(sg_image_desc){
            .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8,
            .usage.dynamic_update = true, .label = "fxtk-image" });
        s_img_view = sg_make_view(&(sg_view_desc){ .texture = { .image = s_img_tex }, .label = "fxtk-image-view" });
        s_img_w = w; s_img_h = h;
        if (s_img_cap < w * h) { free(s_img_stage); s_img_stage = (uint32_t *)malloc((size_t)w * h * 4); s_img_cap = w * h; }
    }
    if (!s_img_stage) return;
    for (int i = 0; i < w * h; i++) {
        uint32_t c = px[i] & 0xFFFFFFu;
        if (dark) c = ((c >> 1) & 0x7F7F7Fu);
        s_img_stage[i] = 0xFF000000u | c;
    }
    sg_update_image(s_img_tex, &(sg_image_data){ .mip_levels[0] = { s_img_stage, (size_t)w * h * 4 } });
    int slot = tex_alloc(s_img_tex, s_img_view, w, h);
    if (slot < 0) return;
    emit_quad(1, slot, (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh), 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

static int drv_touch_read(int *x, int *y, int *pressed)
{
    evq_t e;
    while (evq_pop(&e)) {
        if (e.type == 1) { *x = e.x; *y = e.y; *pressed = e.p; return 1; }
        /* 其它类型回塞: 简化处理 —— 只在本函数内消费 touch, 其余留给 key/wheel 队列 */
        evq_t keep = e;
        if (keep.type != 1) {
            /* 放回队首: 用一个小的延迟缓冲 */
            static evq_t defer[EVQ_MAX]; static int dn = 0;
            if (dn < EVQ_MAX) defer[dn++] = keep;
        }
    }
    return 0;
}

static int drv_key_read(fx_keyev_t *ev)
{
    evq_t e;
    while (evq_pop(&e)) {
        if (e.type == 3) { *ev = e.key; return 1; }
        static evq_t defer[EVQ_MAX]; static int dn = 0;
        if (dn < EVQ_MAX) defer[dn++] = e;
    }
    return 0;
}

static int drv_wheel_read(int *x, int *y, int *dy)
{
    evq_t e;
    while (evq_pop(&e)) {
        if (e.type == 2) { *x = e.x; *y = e.y; *dy = e.dy; return 1; }
    }
    return 0;
}

static void drv_set_title(const char *s) { sapp_set_window_title(s ? s : "fxtk"); }
static void drv_set_clip_rect(int x1, int y1, int x2, int y2)
{
    /* 顶点管线用 scissor 表达裁剪: 记录并作为命令属性(简化: 立即生效于当前命令) */
    (void)x1; (void)y1; (void)x2; (void)y2;
}
static void drv_clip_set(const char *s) { (void)s; }
static const char *drv_clip_get(void) { return ""; }
static void drv_blit_img_rot(const uint32_t *px, int w, int h, int cx, int cy, int dw, int dh, double ang)
{
    (void)px; (void)w; (void)h; (void)cx; (void)cy; (void)dw; (void)dh; (void)ang;   /* P2.4: 走 quadrilateral */
}

#if !defined(_WIN32)
#include <GL/gl.h>
#endif

/* 截图: 必须在 pass 内、交换缓冲【之前】回读 —— 交换后再读默认帧缓冲只会得到空白。
 * 因此由 main 在需要的帧上"请求", 本驱动在 sg_commit() 前抓取并暂存。 */
static uint32_t *s_shot_buf = NULL;
static int s_shot_w = 0, s_shot_h = 0, s_shot_cap = 0, s_shot_req = 0, s_shot_ready = 0;

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
    s_w = fb_w; s_h = fb_h;
    {   /* FPS 统计 (与 SDL 驱动同款语义: 每秒刷新一次) */
        static uint32_t t0 = 0;
        uint32_t now = (uint32_t)fx_time_ms();
        if (!t0) t0 = now;
        s_fps_n++;
        if (now - t0 >= 1000) { s_fps = s_fps_n; s_fps_n = 0; t0 = now; }
    }
    px_flush();
    px_upload();
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
        sg_apply_pipeline(c->pip == 0 ? s_pip_solid : s_pip_tex);
        sg_bindings b = {0};
        b.vertex_buffers[0] = s_vbuf;
        b.index_buffer = s_ibuf;
        if (c->pip == 1 && c->tex >= 0) {
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

    if (getenv("FXTK_STAT")) {
        static int n = 0;
        if ((n++ % 30) == 0)
            fprintf(stderr, "[stat] 顶点=%d 索引=%d 命令=%d 纹理=%d 分辨率=%dx%d\n",
                    s_vb_n, s_ib_n, s_cmd_n, s_tex_n, s_w, s_h);
    }
    s_vb_n = 0; s_ib_n = 0; s_cmd_n = 0; s_tex_n = 0;
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

/* ================= sokol_app 事件 → 框架轮询队列 =================
 * 语义与 SDL 驱动对齐: TEXTINPUT/CHAR → key=0 的文本事件; 特殊键 → fx_key* 常量;
 * Ctrl+C/V/X/A → utf8[0]=字母 + mod; mod 位: 1=Ctrl, 2=Shift。 */
void fxtk_sokol_handle_event(const sapp_event *e)
{
    static int s_mouse_down = 0;
    evq_t q;
    memset(&q, 0, sizeof(q));
    switch (e->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) { s_rc = 1; s_rc_x = (int)e->mouse_x; s_rc_y = (int)e->mouse_y; break; }
        s_mouse_down = 1; q.type = 1; q.x = (int)e->mouse_x; q.y = (int)e->mouse_y; q.p = 1; evq_push(&q); break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        s_mouse_down = 0; q.type = 1; q.x = (int)e->mouse_x; q.y = (int)e->mouse_y; q.p = 0; evq_push(&q); break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        q.type = 1; q.x = (int)e->mouse_x; q.y = (int)e->mouse_y; q.p = s_mouse_down; evq_push(&q); break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        q.type = 2; q.x = (int)e->mouse_x; q.y = (int)e->mouse_y; q.dy = (int)(e->scroll_y * 40.0f); evq_push(&q); break;
    case SAPP_EVENTTYPE_TOUCHES_BEGAN:
    case SAPP_EVENTTYPE_TOUCHES_MOVED:
    case SAPP_EVENTTYPE_TOUCHES_ENDED:
        for (int i = 0; i < e->num_touches && i < 1; i++) {
            q.type = 1; q.x = (int)e->touches[i].pos_x; q.y = (int)e->touches[i].pos_y;
            q.p = (e->type != SAPP_EVENTTYPE_TOUCHES_ENDED);
            if (e->type == SAPP_EVENTTYPE_TOUCHES_ENDED && e->num_touches == 0) q.p = 0;
            evq_push(&q);
        }
        break;
    case SAPP_EVENTTYPE_CHAR: {
        uint32_t cp = e->char_code;
        char *o = q.key.utf8;
        if (cp < 0x80) { o[0] = (char)cp; o[1] = 0; }
        else if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); o[2] = 0; }
        else if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); o[3] = 0; }
        else { o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); o[4] = 0; }
        q.type = 3; q.key.key = 0; q.key.down = 1;
        evq_push(&q);
        break; }
    case SAPP_EVENTTYPE_KEY_DOWN: {
        if (e->key_repeat) break;
        int k = 0;
        switch (e->key_code) {
        case SAPP_KEYCODE_BACKSPACE: k = FX_KEY_BACKSPACE; break;
        case SAPP_KEYCODE_ENTER:     k = FX_KEY_RETURN; break;
        case SAPP_KEYCODE_ESCAPE:    k = FX_KEY_ESCAPE; break;
        case SAPP_KEYCODE_LEFT:      k = FX_KEY_LEFT; break;
        case SAPP_KEYCODE_RIGHT:     k = FX_KEY_RIGHT; break;
        case SAPP_KEYCODE_HOME:      k = FX_KEY_HOME; break;
        case SAPP_KEYCODE_END:       k = FX_KEY_END; break;
        case SAPP_KEYCODE_UP:        k = FX_KEY_UP; break;
        case SAPP_KEYCODE_DOWN:      k = FX_KEY_DOWN; break;
        case SAPP_KEYCODE_DELETE:    k = FX_KEY_DELETE; break;
        default: break;
        }
        int mod = ((e->modifiers & SAPP_MODIFIER_CTRL) ? 1 : 0) | ((e->modifiers & SAPP_MODIFIER_SHIFT) ? 2 : 0);
        s_shift = (mod & 2) ? 1 : 0;
        if (k) { q.type = 3; q.key.key = k; q.key.down = 1; q.key.mod = mod; evq_push(&q); }
        else if (mod & 1) {
            char c = 0;
            if (e->key_code == SAPP_KEYCODE_C) c = 'c';
            else if (e->key_code == SAPP_KEYCODE_V) c = 'v';
            else if (e->key_code == SAPP_KEYCODE_X) c = 'x';
            else if (e->key_code == SAPP_KEYCODE_A) c = 'a';
            else if (e->key_code == SAPP_KEYCODE_L) c = 'l';
            if (c) { q.type = 3; q.key.key = 0; q.key.down = 1; q.key.mod = mod; q.key.utf8[0] = c; evq_push(&q); }
        }
        break; }
    case SAPP_EVENTTYPE_RESIZED:
        s_w = sapp_width(); s_h = sapp_height();
        fx_driver_t *d = &fx_sokol_driver;
        d->width = (uint32_t)s_w; d->height = (uint32_t)s_h;
        free(s_fb); s_fb = NULL; s_fb_w = s_fb_h = 0; s_dirty = 0;
        fb_ensure();
        s_need_clear = 1;   /* 尺寸变了 → 帧缓冲内容未定义, 清一次 */
        break;
    default: break;
    }
}
