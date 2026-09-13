#include <stdlib.h>
/**
 * app.c — fxtk 综合演示 (5 页: 波形/动效/控件/图片/3D光追)
 */
#include "fxtk.h"
#include "fxtk_backends.h"
#include <stdarg.h>
#include "fxtk_image.h"
#include "fxtk_desktop.h"
#include "fxtk_effects.h"
#include "raymarch.h"
#include "gpu_raymarch.h"
/* 颜色选择器 (控件页): 两套 RGB —— 背景(改按钮底色) + 文字(改按钮字色) */
static int s_cr=255,s_cg=90,s_cb=0, s_tr=40,s_tg=40,s_tb=40;
static void cp_apply(void){
    fx_widget_t*b=fx_find("cp_btn");
    if (b){ fx_set_color_w(b,FX_RGB(s_cr,s_cg,s_cb)); fx_set_fgcolor_w(b,FX_RGB(s_tr,s_tg,s_tb)); }
    fx_widget_t*sw=fx_find("cp_sw");
    if (sw) fx_set_color_w(sw,FX_RGB(s_cr,s_cg,s_cb));
}
static int cp_num(const char*n){ if(!n||!n[0])return 0; int v=atoi(n);
if (v<0)v=0;
if (v>255)v=255; return v; }
static void cp_cb(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    s_cr=cp_num(fx_textedit_text(fx_find("cp_br"))); s_cg=cp_num(fx_textedit_text(fx_find("cp_bg"))); s_cb=cp_num(fx_textedit_text(fx_find("cp_bb")));
    s_tr=cp_num(fx_textedit_text(fx_find("cp_tr"))); s_tg=cp_num(fx_textedit_text(fx_find("cp_tg"))); s_tb=cp_num(fx_textedit_text(fx_find("cp_tb")));
    cp_apply(); }
extern void build_desktop_pages(void);
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

static int s_clicks[9] = { 0 };
static int s_phase = 0;
static int s_wave_on = 1;
static int s_speed = 30;
static int s_pic = 0;
static fx_image_t *s_pics[3];
static void img_info_set(const char *fmt, ...);   /* 前向声明 (on_img 早于其定义) */
/* v2.4 页3 四边形形变编辑器状态 (声明须早于 on_img/on_zoom) */
static float s_quad_n[8] = { 0.04f, 0.04f, 0.96f, 0.04f, 0.96f, 0.96f, 0.04f, 0.96f };   /* 四角(归一化, 内缩 4% 让手柄可见) */
static float s_quad_scale = 1.0f;                        /* 缩放(以中心为基准) */
static int   s_quad_drag = -1;
static int   s_quad_show = 1;
static fx_image_t *s_imported = NULL;                    /* 导入的图片 */
static int s_gfx_t = 0, s_spin = 30, s_gfx_mode = 0;
#define TRAIL_N 14
static int s_tx[TRAIL_N], s_ty[TRAIL_N], s_ti = 0;
/* 3D 页状态 */
static fx_image_t *s_rt = NULL;
static int s_oc = 0;
static int s_gpu = 1;                 /* v2.4: 默认用 GPU 光追(无 GPU 后端时自动回退 CPU) */
static float s_rt_time = 0;
static int s_rspeed = 30, s_rqual = 50;
static int s_frames = 0;
static long s_fps_t0 = 0;
static const char *s_keys[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void on_key(fx_widget_t *w, void *ud) {
    int id = (int)(intptr_t)ud;
    s_clicks[id]++;
    char buf[48];
    snprintf(buf, sizeof(buf), "第 %d 键 · 累计 %d 次", id + 1, s_clicks[id]);
    fx_set_title(fx_find("info"), buf);
}
static void on_speed(fx_widget_t *w, void *ud) {
    s_speed = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), s_speed);
}
static void on_wave(fx_widget_t *w, void *ud) { s_wave_on = fx_get_value(w) != 0; }
static void on_reset(fx_widget_t *w, void *ud) {
    for (int i = 0; i < 9; i++) s_clicks[i] = 0;
    s_phase = 0;
    fx_set_title(fx_find("info"), "已重置 · 点击数字键试试");
}
static void on_img(fx_widget_t *w, void *ud) {
    (void)w; (void)ud;
    s_pic = (s_pic + 1) % 3;
    if (s_imported) { fx_image_free(s_imported); s_imported = NULL; }   /* 换回内置图案 */
    static const float def[8] = { 0.04f, 0.04f, 0.96f, 0.04f, 0.96f, 0.96f, 0.04f, 0.96f };
    memcpy(s_quad_n, def, sizeof(def));
    img_info_set("内置图案 %d / 3", s_pic + 1);
}
static void on_zoom(fx_widget_t *w, void *ud) {
    (void)ud;
    s_quad_scale = 0.4f + (float)fx_get_value(w) / 100.0f * 1.6f;   /* 0.4x ~ 2.0x */
}
static void on_spin(fx_widget_t *w, void *ud) { s_spin = fx_get_value(w); }
static void on_mode(fx_widget_t *w, void *ud) {
    s_gfx_mode = (s_gfx_mode + 1) % 4;
    const char *m[4] = { "模式: 全部", "模式: 贴图", "模式: 矢量", "模式: 伪3D(四边形形变)" };
    fx_set_title(fx_find("gfx_info"), m[s_gfx_mode]);
}

/* ================= v2.4 页3: 图片四边形形变编辑器 ================= */
static void img_info_set(const char *fmt, ...)
{
    char buf[160];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fx_set_title(fx_find("img_info"), buf);
}

static void quad_corner_px(int cw, int ch, int i, float *ox, float *oy)
{
    *ox = (0.5f + (s_quad_n[i * 2] - 0.5f) * s_quad_scale) * (float)cw;
    *oy = (0.5f + (s_quad_n[i * 2 + 1] - 0.5f) * s_quad_scale) * (float)ch;
}

static void on_imgq(fx_widget_t *w, void *ud)
{
    (void)ud;
    int cw, ch; fx_canvas_size(w, &cw, &ch);
    fx_canvas_clear(w, FX_RGB(245, 245, 245));
    fx_image_t *img = s_imported ? s_imported : s_pics[s_pic];
    if (!img) return;

    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int mx, my, mp; fx_touch_state(&mx, &my, &mp);
    int lx = mx - x1, ly = my - y1;                      /* 画布本地坐标 */

    /* 命中手柄 → 开始拖; 松手结束 */
    if (mp && s_quad_drag < 0) {
        for (int i = 0; i < 4; i++) {
            float hx, hy; quad_corner_px(cw, ch, i, &hx, &hy);
            if (lx >= (int)hx - 10 && lx <= (int)hx + 10 && ly >= (int)hy - 10 && ly <= (int)hy + 10) { s_quad_drag = i; break; }
        }
    }
    if (!mp) s_quad_drag = -1;
    if (s_quad_drag >= 0) {                              /* 反解回归一化坐标 */
        float nx = (float)lx / (float)cw, ny = (float)ly / (float)ch;
        s_quad_n[s_quad_drag * 2]     = 0.5f + (nx - 0.5f) / (s_quad_scale > 0.01f ? s_quad_scale : 1.0f);
        s_quad_n[s_quad_drag * 2 + 1] = 0.5f + (ny - 0.5f) / (s_quad_scale > 0.01f ? s_quad_scale : 1.0f);
    }

    /* 画四边形 (真透视) */
    float q[8];
    for (int i = 0; i < 4; i++) quad_corner_px(cw, ch, i, &q[i * 2], &q[i * 2 + 1]);
    fx_draw_image_quad(img, q);

    fx_draw_text_c(8, ch - 20, "拖动四角顶点 → 任意四边形", FX_RGB(90, 90, 90), FX_RGB(245, 245, 245));   /* 提示放画布左下, 避开手柄 */
    /* 手柄 + 边线 */
    if (s_quad_show) {
        fx_set_color(FX_RGB(200, 60, 60));
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            fx_draw_line((int)q[i*2], (int)q[i*2+1], (int)q[j*2], (int)q[j*2+1]);
        }
        for (int i = 0; i < 4; i++) {
            int hx = (int)q[i*2], hy = (int)q[i*2+1];
            int near_h = (abs(lx - hx) <= 14 && abs(ly - hy) <= 14);
            fx_set_color(s_quad_drag == i || near_h ? FX_RGB(255, 160, 0) : FX_RGB(255, 255, 255));
            fx_fill_rect(hx - 7, hy - 7, hx + 7, hy + 7);
            fx_set_color(FX_RGB(160, 40, 40));
            fx_draw_rect(hx - 8, hy - 8, hx + 8, hy + 8);
            fx_draw_rect(hx - 7, hy - 7, hx + 7, hy + 7);
        }
    }
}

static void on_img_load(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    char path[512];
    /* 无头/CI 与脚本化验证: FXTK_IMPORT=<路径> 直接导入, 不走系统对话框
     * (Linux 上对话框依赖 zenity/kdialog, 服务器与 CI 里通常没有)。 */
    const char *env_import = getenv("FXTK_IMPORT");
    if (env_import && env_import[0]) snprintf(path, sizeof(path), "%s", env_import);
    else if (!fx_backend_pick_file(path, (int)sizeof(path), "选择图片", "png,jpg,jpeg,bmp,gif,tga")) {
        img_info_set("已取消 / 对话框不可用 (可设 FXTK_IMPORT=<路径> 直接导入)");
        return;
    }
    fx_image_t *img = fx_image_load(path);
    if (!img) { img_info_set("解码失败: %s", fx_path_basename(path)); return; }
    if (s_imported) fx_image_free(s_imported);
    s_imported = img;
    s_quad_scale = 1.0f;
    img_info_set("已导入 %s (%dx%d)", fx_path_basename(path), img->w, img->h);
}

static void on_img_reset(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    static const float def[8] = { 0.04f, 0.04f, 0.96f, 0.04f, 0.96f, 0.96f, 0.04f, 0.96f };
    memcpy(s_quad_n, def, sizeof(def));
    s_quad_scale = 1.0f;
    s_quad_show = 1;
    img_info_set("四边形已复位");
}

static void on_canvas(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(0x080808); fx_fill_rect(0, 0, cw - 1, ch - 1);
    fx_set_color(0x101410);
    for (int x = 0; x < cw; x += 16) fx_draw_vline(x, 0, ch - 1);
    for (int y = 0; y < ch; y += 16) fx_draw_hline(0, cw - 1, y);
    if (s_wave_on) {
        int amp = ch / 2 - 8, prev_y = ch / 2;
        static const float PI2_32 = 3.14159265f / 32.0f;
        fx_set_color(FX_YELLOW);
        for (int x = 0; x < cw; x++) {
            float t = (float)(x + s_phase) * PI2_32;
            int y = ch / 2 + (int)(amp * sinf(t) * 0.7f + amp * 0.3f * sinf(t / 3.0f));
            fx_draw_line(x > 0 ? x - 1 : 0, prev_y, x, y);   /* x=0 时别用 -1: 会越出画布左边界 1px */
            prev_y = y;
        }
        fx_set_color(FX_RED);
        fx_fill_circle(cw / 2, ch / 2 + (int)(amp * sinf((float)(cw / 2 + s_phase) * PI2_32) * 0.7f), 6);
    }
    s_phase += s_speed;
    s_phase %= 4096;
}

static void on_pt(fx_widget_t *w, void *ud);
static void on_pt_slider(fx_widget_t *w, void *ud);
/* 六边形顶点(单位R)静态表, 免每帧重复算 cosf/sinf; R=40, y 压 0.85 */
static const int16_t HEX_PTS[12] = {
     40,    0,   20,   29,  -20,   29,
    -40,    0,  -20,  -29,   20,  -29,
};

/* ================= v2.4 P4: 图形页伪 3D 场景 =================
 * 全部由 fx_draw_image_quad(真透视四边形形变)拼出来, 没有 3D 管线:
 *   地板/天花板 = 一格一个四边形(近大远小); 走廊 = 两侧墙面四边形序列;
 *   立方体 = 6 个面各一次形变 + 画家算法(远面先画); 公告板 = 屏幕空间四边形, 永远面向相机。
 * HUD 显示四边形数 / fps / 形变走的哪条路径(GPU 钩子或 CPU 逆单应)。 */
static fx_image_t *s_p3_floor = NULL, *s_p3_wall = NULL, *s_p3_spr = NULL;
static int   s_p3_n = 0;
static float s_p3_t = 0.0f, s_p3_cube = 0.0f;   /* t: 摆动相位 */

static void p3_tiles(void)
{
    if (s_p3_floor) return;
    s_p3_floor = fx_image_create(64, 64);
    s_p3_wall  = fx_image_create(64, 64);
    s_p3_spr   = fx_image_create(32, 32);
    if (!s_p3_floor || !s_p3_wall || !s_p3_spr) return;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int c = (((x >> 3) + (y >> 3)) & 1);
        fx_image_set_px(s_p3_floor, x, y, c ? FX_RGB(206, 210, 218) : FX_RGB(116, 122, 132));
        int b = (((x >> 4) & 1) ^ ((y >> 2) & 1));
        fx_image_set_px(s_p3_wall, x, y, b ? FX_RGB(158, 96, 74) : FX_RGB(122, 68, 54));
    }
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        int dx = x - 16, dy = y - 16;
        fx_image_set_px(s_p3_spr, x, y, (dx*dx + dy*dy) < 210 ? FX_RGB(255, 214, 64) : FX_RGB(24, 40, 78));
    }
}

/* 世界 → 画布局部屏幕坐标。相机在 (0,1.45,0) 朝 +z, 场景绕 Y 旋转 a。Z 太近就丢片。 */
static int p3_proj(float wx, float wy, float wz, float a, int cw, int ch, float *sx, float *sy, float *z)
{
    float c = cosf(a), s = sinf(a);
    float X = wx * c - wz * s, Z = wx * s + wz * c, Y = wy - 1.45f;
    if (Z < 0.35f) return 0;
    float f = (float)cw * 1.05f;
    *sx = (float)cw * 0.5f + f * X / Z;
    *sy = (float)ch * 0.60f - f * Y / Z;
    *z  = Z;
    return 1;
}

/* 世界坐标 4 角 → 四边形形变; 任一角投影失败(跑到相机后面)则整片跳过 */
static void p3_face(fx_image_t *tx, float a, int cw, int ch,
                    const float *wx, const float *wy, const float *wz)
{
    float q[8];
    for (int i = 0; i < 4; i++) {
        float sx, sy, z;
        if (!p3_proj(wx[i], wy[i], wz[i], a, cw, ch, &sx, &sy, &z)) return;
        q[i * 2] = sx; q[i * 2 + 1] = sy;
    }
    fx_draw_image_quad(tx, q);
    s_p3_n++;
}

static void p3_scene(int cw, int ch)
{
    p3_tiles();
    if (!s_p3_floor) return;
    s_p3_n = 0;
    /* 相机左右摆动而不是让世界连续旋转: 连续旋转转到背面时整条走廊都在相机后面、
     * 所有四边形被投影丢弃 → 画面整个变空(HUD 里四边形数从 105 掉到 7, 用户报的"伪3D到后面没了")。
     * 用有界摆动(±22°, 周期约 14s)保证走廊永远在相机前方。 */
    float a = 0.38f * sinf(s_p3_t * 0.45f);
    fx_set_color(FX_RGB(8, 10, 18)); fx_fill_rect(0, 0, cw - 1, ch - 1);

    /* 地板 + 天花板: 每格一个四边形, 透视天然近大远小 */
    for (int iz = 0; iz < 8; iz++) {
        for (int ix = -2; ix <= 2; ix++) {
            float x0 = ix * 1.2f, x1 = x0 + 1.2f, z0 = 1.6f + iz * 1.1f, z1 = z0 + 1.1f;
            float wx[4] = { x0, x1, x1, x0 };
            float wz[4] = { z0, z0, z1, z1 };
            float wy[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            p3_face(s_p3_floor, a, cw, ch, wx, wy, wz);
            wy[0] = wy[1] = wy[2] = wy[3] = 2.9f;              /* 天花板 */
            p3_face(s_p3_floor, a, cw, ch, wx, wy, wz);
        }
    }
    /* 走廊两侧墙: 按 z 分段, 每段一个四边形 */
    for (int iz = 0; iz < 8; iz++) {
        float z0 = 1.6f + iz * 1.1f, z1 = z0 + 1.1f;
        for (int side = 0; side < 2; side++) {
            float x = side ? 2.6f : -2.6f;
            float wx[4] = { x, x, x, x };
            float wz[4] = { z0, z0, z1, z1 };
            float wy[4] = { 0.0f, 2.9f, 2.9f, 0.0f };
            p3_face(s_p3_wall, a, cw, ch, wx, wy, wz);
        }
    }
    /* 旋转纹理立方体: 6 个面各一次形变 + 画家算法排序 */
    {
        static const int FI[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {4,5,1,0}, {3,2,6,7} };
        float V[8][3];
        float ca = cosf(s_p3_cube), sa = sinf(s_p3_cube);
        float cb = cosf(s_p3_cube * 0.7f), sb = sinf(s_p3_cube * 0.7f);
        for (int i = 0; i < 8; i++) {
            float x = (i & 1) ? 0.42f : -0.42f;
            float y = (i & 2) ? 0.42f : -0.42f;
            float z = (i & 4) ? 0.42f : -0.42f;
            float x1 = x * ca - z * sa, z1 = x * sa + z * ca;
            float y1 = y * cb - z1 * sb, z2 = y * sb + z1 * cb;
            V[i][0] = x1; V[i][1] = y1 + 1.45f; V[i][2] = z2 + 4.2f;
        }
        float fz[6]; int order[6];
        for (int i = 0; i < 6; i++) {
            order[i] = i; fz[i] = 0.0f;
            for (int k = 0; k < 4; k++) fz[i] += V[FI[i][k]][2];
        }
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 6; j++)
                if (fz[order[j]] > fz[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        for (int i = 0; i < 6; i++) {
            int fi = order[i];
            float wx[4], wy[4], wz[4];
            for (int k = 0; k < 4; k++) { wx[k] = V[FI[fi][k]][0]; wy[k] = V[FI[fi][k]][1]; wz[k] = V[FI[fi][k]][2]; }
            p3_face(s_p3_floor, a, cw, ch, wx, wy, wz);
        }
    }
    /* 公告板精灵: 屏幕空间四边形, 永远正对相机(伪 3D 常用手法) */
    for (int k = 0; k < 3; k++) {
        float sx, sy, z;
        if (!p3_proj(-1.8f + k * 1.8f, 1.5f, 2.6f + k * 1.6f, a, cw, ch, &sx, &sy, &z)) continue;
        float s = 26.0f / z * ((float)cw / 440.0f) * 3.0f;
        float q[8] = { sx - s, sy - s, sx + s, sy - s, sx + s, sy + s, sx - s, sy + s };
        fx_draw_image_quad(s_p3_spr, q);
        s_p3_n++;
    }
    {   /* HUD: 四边形数 / fps / 形变路径 */
        char buf[96];
        snprintf(buf, sizeof(buf), "四边形 %d · fps %d · %s", s_p3_n, fxtk_fps(),
                 fx_quad_warp_gpu() ? "GPU 形变" : "CPU 形变");
        fx_draw_text_c(6, ch - 16, buf, FX_WHITE, FX_RGB(8, 10, 18));
    }
    s_p3_t += 1.0f;
    s_p3_cube += 0.020f;
}

static void on_gfx(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    if (s_gfx_mode == 3) { p3_scene(cw, ch); return; }   /* v2.4: 伪 3D 场景(纯四边形形变) */
    float ks = cw / 440.0f;                 /* 【新增】图形随画布等比缩放 */
    if (ks < 0.5f) ks = 0.5f;
    int ang = s_gfx_t % 360;
    fx_set_color(0x000083); fx_fill_rect(0, 0, cw - 1, ch - 1);
    if (s_gfx_mode != 2 && s_pics[0]) {
        fx_draw_image_rot(s_pics[0], cw / 4, ch / 2, ang, (int)(90 * ks));
        fx_draw_image_rot(s_pics[2], cw * 3 / 4, ch / 2, -ang, (int)(70 * ks));
    }
    if (s_gfx_mode != 1) {
        int16_t hex[12];
        for (int k = 0; k < 12; k++) hex[k] = (int16_t)(HEX_PTS[k] * ks);
        fx_set_color(FX_MAGENTA);
        fx_fill_polygon_rot(hex, 6, cw / 2, ch / 2, ang);
        int T = (int)(26 * ks);
        int16_t tri[6] = { 0, (int16_t)-T, (int16_t)(T * 0.85), (int16_t)(T / 2),
                           (int16_t)(-T * 0.85), (int16_t)(T / 2) };
        fx_set_color(FX_YELLOW);
        fx_fill_polygon_rot(tri, 3, cw / 2, ch / 2, -ang * 2);
        int bx = cw / 2 + (int)((cw / 2 - 50 * ks) * sinf(s_gfx_t * 0.021f));
        int by = ch / 2 + (int)((ch / 2 - 40 * ks) * cosf(s_gfx_t * 0.034f));
        fx_set_color(0x838183);
        for (int k = 0; k < TRAIL_N; k++) {
            int idx = (s_ti - 1 - k + TRAIL_N * 4) % TRAIL_N;
            int r = (int)((5 - k / 3) * ks);
            if (r > 0) fx_fill_circle(s_tx[idx], s_ty[idx], r);
        }
        s_tx[s_ti] = bx; s_ty[s_ti] = by;
        s_ti = (s_ti + 1) % TRAIL_N;
        fx_set_color(FX_WHITE); fx_draw_circle(bx, by, (int)(8 * ks));
        fx_set_color(FX_RED);   fx_fill_circle(bx, by, (int)(6 * ks));
    }
    fx_set_color(FX_CYAN);
    fx_fill_rect_round(cw - (int)(70 * ks), (int)(8 * ks), cw - (int)(10 * ks), (int)(44 * ks), (int)(8 * ks));
    fx_set_color(FX_WHITE);
    fx_draw_arc(cw - (int)(40 * ks), ch - (int)(40 * ks), (int)(24 * ks), 0, 270);
    s_gfx_t += 1 + s_spin / 10;
}

/* ---------- 3D 页: 光线步进 ---------- */
static void on_rspeed(fx_widget_t *w, void *ud) { s_rspeed = fx_get_value(w); }
static void on_rqual(fx_widget_t *w, void *ud) { s_rqual = fx_get_value(w); }
static void on_oc(fx_widget_t *w, void *ud) {
    s_oc = !s_oc;
    fx_set_title(w, s_oc ? "超频: 开" : "超频: 关");
}
static void on_gpu(fx_widget_t *w, void *ud) {
    s_gpu = !s_gpu;
    fx_set_title(w, s_gpu ? "GPU: 开" : "GPU: 关");
    fx_repaint();
}
static void on_3d(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    int rw = s_oc ? cw : (cw > 640 ? 640 : cw);
    int rh = s_oc ? ch : (ch > 360 ? 360 : ch);
    if (rw < 16) rw = 16;
    if (rh < 16) rh = 16;
    if (!s_rt || s_rt->w != rw || s_rt->h != rh) {
        if (s_rt) fx_image_free(s_rt);
        s_rt = fx_image_create(rw, rh);
        if (!s_rt) return;
    }
    /* v2.4: 三条路径, 优先级从快到慢 ——
     *   ① fx_raymarch_available(): 后端提供片元着色器级 raymarch(sokol), 直接在当前矩形里算,
     *      不占 CPU 像素、不做回读、分辨率跟窗口走(真·GPU);
     *   ② gpu_raymarch_ok(): 旧的独立 GL 上下文通道(默认关, 部分 NVIDIA 驱动不稳);
     *   ③ CPU 软件光线步进: 无 GPU 平台(ESP32/测试)的兜底, 有分辨率上限。 */
    int gpu_direct = (s_gpu && fx_raymarch_available());
    if (gpu_direct) {
        fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1);
        fx_draw_raymarch(s_rt_time, 0, 0, cw, ch);      /* 由驱动把视口限定到该矩形 */
    } else {
        if (s_gpu && gpu_raymarch_ok()) gpu_raymarch_render(s_rt->px, rw, rh, s_rt_time);
        else                            raymarch_render(s_rt->px, rw, rh, s_rt_time, 24 + s_rqual);
        fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1);
        fx_draw_image(s_rt, 0, 0, cw, ch);
    }
    s_rt_time += 0.016f * (0.3f + s_rspeed * 0.06f);

    /* FPS 统计 (每 500ms 刷新) */
    s_frames++;
    long now = now_ms();
    if (!s_fps_t0) s_fps_t0 = now;
    if (now - s_fps_t0 >= 500) {
        char buf[48];
        snprintf(buf, sizeof(buf), "FPS: %d (%dx%d) [%s]",
                 (int)(s_frames * 1000L / (now - s_fps_t0)), gpu_direct ? cw : rw, gpu_direct ? ch : rh,
                 gpu_direct ? "GPU 着色器" : ((s_gpu && gpu_raymarch_ok()) ? gpu_raymarch_renderer() : "CPU"));
        fx_set_title(fx_find("fps_lbl"), buf);
        s_frames = 0; s_fps_t0 = now;
    }
}

static fx_image_t *make_pic(int kind) {
    fx_image_t *im = fx_image_create(120, 90);
    if (!im) return NULL;
    for (int y = 0; y < 90; y++)
        for (int x = 0; x < 120; x++) {
            fx_color_t c;
            if (kind == 0)      c = FX_RGB(x * 255 / 119, y * 255 / 89, 140);
            else if (kind == 1) c = ((x / 10 + y / 10) & 1) ? FX_RGB(45, 45, 52) : FX_RGB(240, 240, 240);
            else { int dx = x - 60, dy = y - 45; c = (dx * dx + dy * dy < 900) ? FX_RGB(244, 67, 54) : FX_RGB(33, 150, 243); }
            fx_image_set_px(im, x, y, c);
        }
    return im;
}

static void build_ui(void) {
    fx_label_new(percent("0.02,0.01", "0.98,0.09"), title("fxtk 演示 · 标签页"), fgcolor(FX_RGB(51, 51, 51)));
    fx_tab_new(pixel("10,26", "470,266"), title("波形,图形,控件,图片,3D,输入,画板,键鼠,压测,滚动,组件,粒子"), name("tab"), color(FX_RGB(224, 224, 224)));
    fx_parent(fx_find("tab"));

    fx_canvas_new(pixel("6,32", "444,188"), name("wave_cv"), page(0), anim(1), color(FX_RGB(30, 30, 30)), call(on_canvas));
    fx_label_new(pixel("6,194", "56,212"), page(0), title("速度"), fgcolor(FX_RGB(51, 51, 51)));
    fx_slider_new(name("wv_s1"), pixel("60,194", "292,212"), name("speed"), page(0), value(s_speed), color(FX_RGB(76, 175, 80)), call(on_speed));
    fx_progress_new(name("wv_prog"), pixel("300,196", "444,210"), name("speed_bar"), page(0), value(s_speed));
    fx_checkbox_new(pixel("6,218", "150,240"), name("wv_chk"), title("波形开关"), name("wave"), page(0), value(1), fgcolor(FX_RGB(51, 51, 51)), call(on_wave));

    fx_canvas_new(pixel("6,32", "444,196"), name("gfx_cv"), page(1), anim(1), color(FX_RGB(30, 30, 30)), call(on_gfx));
    fx_parent(fx_find("gfx_cv"));   /* C2: canvas 当容器 */
    fx_button_new(pixel("20,40", "90,64"), page(1), title("内嵌"), color(FX_RGB(255,152,0)), call(on_mode));
    fx_parent(fx_find("tab"));      /* 恢复 */
    fx_button_new(pixel("6,202", "110,236"), name("sh_btn"), page(1), title("切换模式"), color(FX_RGB(33, 150, 243)), call(on_mode));
    fx_slider_new(pixel("120,208", "300,222"), name("spin"), page(1), value(s_spin), color(FX_RGB(244, 67, 54)), call(on_spin));
    fx_label_new(pixel("310,204", "444,236"), name("gfx_info"), page(1), title("模式: 全部"), fgcolor(FX_RGB(51, 51, 51)));

    fx_grid_map(pixel("8,54", "252,214"), line(3), row(3), name("keys"), page(2), dense());
    for (int i = 0; i < 9; i++) {
        fx_widget_t *b = fx_button_new(grid("keys", i / 3 + 1, i % 3 + 1, i / 3 + 1, i % 3 + 1),
                                       title(s_keys[i]), color(FX_RGB(33, 150, 243)), page(2));
        fx_set_cb(b, on_key, (void *)(intptr_t)i);
    }
    /* 颜色选择器 (右侧): 两套 RGB 改示例按钮底色/字色, 干净排布 */
    fx_label_new(pixel("262,36","470,50"), page(2), title("颜色选择器"), fgcolor(FX_RGB(51,51,51)));
    fx_label_new(pixel("262,56","306,70"), page(2), title("背景"), fgcolor(FX_RGB(120,120,120)));
    fx_textedit_new(pixel("262,72","330,90"), name("cp_br"), page(2), title("255"), call(cp_cb));
    fx_textedit_new(pixel("334,72","402,90"), name("cp_bg"), page(2), title("90"), call(cp_cb));
    fx_textedit_new(pixel("406,72","470,90"), name("cp_bb"), page(2), title("0"), call(cp_cb));
    fx_label_new(pixel("262,96","306,110"), page(2), title("文字"), fgcolor(FX_RGB(120,120,120)));
    fx_textedit_new(pixel("262,112","330,130"), name("cp_tr"), page(2), title("40"), call(cp_cb));
    fx_textedit_new(pixel("334,112","402,130"), name("cp_tg"), page(2), title("40"), call(cp_cb));
    fx_textedit_new(pixel("406,112","470,130"), name("cp_tb"), page(2), title("40"), call(cp_cb));
    fx_button_new(pixel("262,142","470,164"), page(2), title("示例按钮"), name("cp_btn"), color(FX_RGB(255,90,0)), call(cp_cb));
    fx_label_new(pixel("262,172","470,192"), page(2), name("cp_sw"), title("颜色显示区"), fgcolor(FX_WHITE), color(FX_RGB(255,90,0)));
    fx_button_new(pixel("262,200","352,220"), page(2), title("重置"), color(FX_RGB(244, 67, 54)), call(on_reset));
fx_label_new(pixel("262,224","470,236"), name("info"), page(2), title("点击数字键试试"), fgcolor(FX_RGB(51, 51, 51)));
    /* v2.4 P5: 卡片示范 —— 圆角 + 1px 描边的小面板, 托住整组"颜色选择器"。
     * 创建顺序有讲究: 同级新控件排在链表头、绘制时先画(等于最底层), 所以卡片必须【最后】创建。 */
    fx_panel_new(pixel("256,28","470,240"), page(2), color(FX_RGB(250, 250, 250)), border(1));

    /* 页3 (v2.4): 图片 + 四边形形变编辑器 */
    fx_canvas_new(pixel("6,32", "280,220"), name("imgq"), page(3), anim(1), color(FX_RGB(245, 245, 245)), call(on_imgq));
    fx_button_new(pixel("292,36", "444,58"), name("img_load"), page(3), title("导入图片…"), color(FX_RGB(33, 150, 243)), call(on_img_load));
    fx_button_new(pixel("292,64", "366,86"), name("img_next"), page(3), title("换图案"), call(on_img));
    fx_button_new(pixel("372,64", "444,86"), name("img_reset"), page(3), title("复位"), color(FX_RGB(244, 67, 54)), call(on_img_reset));
    fx_label_new(pixel("292,92", "444,110"), page(3), title("缩放 (以中心为基准)"), fgcolor(FX_RGB(51, 51, 51)));
    fx_slider_new(pixel("292,112", "444,132"), name("zoom"), page(3), value(30), color(FX_RGB(33, 150, 243)), call(on_zoom));
    fx_label_new(pixel("292,142", "444,238"), name("img_info"), page(3), title("拖动四角顶点可任意形变"), fgcolor(FX_RGB(51, 51, 51)));

    /* 页5: 3D 光线步进 */
    fx_canvas_new(pixel("6,32", "444,196"), name("rt_cv"), page(4), anim(1), color(FX_BLACK), call(on_3d));
    fx_canvas_set_buf(fx_find("rt_cv"), 0);   /* 3D 直绘, 放大不空白 */
    fx_canvas_new(pixel("6,32", "444,196"), name("pt_cv"), page(11), anim(1), color(FX_BLACK), call(on_pt));
    fx_slider_new(pixel("6,204", "300,220"), page(11), call(on_pt_slider));
    fx_label_new(pixel("306,204", "444,220"), page(11), title("拖动调粒子数(×200)"), fgcolor(FX_RGB(51, 51, 51)));
    fx_button_new(pixel("6,202", "76,236"), page(4), title("超频: 关"), color(FX_RGB(244, 67, 54)), call(on_oc));
    fx_button_new(pixel("82,202", "152,236"), page(4), title("GPU: 开"), color(FX_RGB(76, 175, 80)), call(on_gpu));
    fx_slider_new(pixel("160,204", "250,218"), name("rspeed"), page(4), value(s_rspeed), color(FX_RGB(33, 150, 243)), call(on_rspeed));
    fx_slider_new(pixel("160,220", "250,234"), name("rqual"), page(4), value(s_rqual), color(FX_RGB(76, 175, 80)), call(on_rqual));
    fx_label_new(pixel("258,202", "444,236"), name("fps_lbl"), page(4), title("FPS: --"), fgcolor(FX_RGB(51, 51, 51)));

    build_desktop_pages();
    fx_parent(NULL);
}


/* 固定像素区域: 工作区拉伸, 控件恒真实像素 (>=640 宽生效) */
/* v1.0: 固定像素 Chrome 延期 v1.1 (需布局系统真实像素锚点), 回调留空 */
static void on_fix(fx_widget_t *w, void *ud)
{
    (void)w; (void)ud;
}

/* ================= 页11: 粒子性能 (GPU 万级图元) ================= */
/* v2.4: 粒子缓冲【按需分配】。原先是 static pt_t s_pt[2000000] —— 常驻 40MB 的 .bss,
 * 对一个以 ESP32 为目标的框架完全不可接受, 而且首次进入要把 200 万粒子全部初始化。
 * 现在只在真正需要时扩容, 且只初始化新增区间; 上限收敛到 40 万(滑杆 0~100 → 0~40 万)。 */
#define PT_MAX 400000
typedef struct { float x,y,vx,vy; uint8_t c; } pt_t;
static pt_t *s_pt = NULL;      /* 动态缓冲(原为 40MB 静态数组) */
static int   s_pt_cap = 0;     /* 已分配容量 */
static int   s_pt_n = 6000;    /* 当前使用数量 */
static fx_image_t *s_ptimg=NULL; static int s_ptimg_w=0, s_ptimg_h=0;
static double s_pt_last = 0; static float s_pt_fps = 0;
static double pt_now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1e6; }
/* 初始化 [from,to) 区间的粒子(位置/速度/颜色随机, 与改造前同一套随机序列) */
static void pt_spawn(int from, int to, int cw, int ch)
{
    for (int i = from; i < to; i++) {
        s_pt[i].x = (float)(rand() % cw); s_pt[i].y = (float)(rand() % ch);
        float a = (float)(rand() % 360) * 0.01745f, sp = 0.5f + (float)(rand() % 100) / 100.0f;
        s_pt[i].vx = cosf(a) * sp; s_pt[i].vy = sinf(a) * sp; s_pt[i].c = (uint8_t)(rand() % 6);
    }
}
/* 保证容量 ≥ n(不足则翻倍扩容), 并把新分配的部分初始化好 */
static int pt_reserve(int n, int cw, int ch)
{
    if (n < 100) n = 100;
    if (n > PT_MAX) n = PT_MAX;
    if (n > s_pt_cap) {
        int nc = s_pt_cap ? s_pt_cap : 6000;
        while (nc < n) nc *= 2;
        if (nc > PT_MAX) nc = PT_MAX;
        pt_t *np = (pt_t *)realloc(s_pt, (size_t)nc * sizeof(pt_t));
        if (!np) return 0;
        s_pt = np;
        pt_spawn(s_pt_cap, nc, cw, ch);
        s_pt_cap = nc;
    }
    s_pt_n = n;
    return 1;
}
static void on_pt_slider(fx_widget_t *w, void *ud){ (void)ud; int v=fx_get_value(w); s_pt_n=v*4000;
if (s_pt_n<100)s_pt_n=100;
if (s_pt_n>PT_MAX)s_pt_n=PT_MAX; }
static void on_pt(fx_widget_t *w, void *ud){
    (void)ud; int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    if (!s_pt_cap) { srand(12345); s_pt_last = pt_now(); }
    if (!pt_reserve(s_pt_n, cw, ch)) return;     /* 首次进入只分配当前需要的量(默认 6000) */
    int hw=cw/2, hh=ch/2;
    if (hw<1)hw=1;
    if (hh<1)hh=1;
    if (!s_ptimg || s_ptimg_w!=hw || s_ptimg_h!=hh){ s_ptimg=fx_image_create(hw,hh); s_ptimg_w=hw; s_ptimg_h=hh; }
    if(!s_ptimg) return;
    double now=pt_now(); double dt=now-s_pt_last; s_pt_last=now;
    if (dt<0.1)dt=0.1;
    if (dt>50)dt=50;
    s_pt_fps=s_pt_fps*0.9f+(float)(1000.0/dt)*0.1f;
    uint32_t *px = s_ptimg->px;
    memset(px, 0, (size_t)hw*hh*sizeof(uint32_t));
    static const uint32_t cols[6]={FX_RED,FX_GREEN,FX_BLUE,FX_YELLOW,FX_CYAN,FX_MAGENTA};
    for(int i=0;i<s_pt_n;i++){ pt_t*p=&s_pt[i];
        p->x+=p->vx*(float)dt*0.06f; p->y+=p->vy*(float)dt*0.06f;
        if (p->x<0)p->x+=(float)cw;
        if (p->x>=cw)p->x-=(float)cw;
        if (p->y<0)p->y+=(float)ch;
        if (p->y>=ch)p->y-=(float)ch;
        int X=(int)p->x>>1, Y=(int)p->y>>1;
        if (X>=0&&X<hw&&Y>=0&&Y<hh) px[Y*hw+X]=cols[p->c]; }
    fx_draw_image(s_ptimg, 0, 0, cw, ch);   /* GPU 2x 放大 blit */
    char buf[64]; snprintf(buf,sizeof(buf),"FPS:%.0f  N:%d  GPU-blit",s_pt_fps,s_pt_n);
    fx_draw_text_c(4,4,buf,FX_GREEN,FX_BLACK);
}
void app_init(void) {
    printf("[I] demo: fxtk demo start (3D Raymarch)\n");
    fxtk_set_fps_debug(0);   /* 3D/粒子页已有 FPS, 不在滚动画布上叠加全局角标 */
    gpu_raymarch_start();   /* GPU 通道后台线程一次性点火 */
    for (int i = 0; i < 3; i++) s_pics[i] = make_pic(i);
    if (s_pics[1]) fx_image_grayscale(s_pics[1]);
    if (s_pics[2]) fx_image_tint(s_pics[2], FX_RGB(0, 200, 255), 90);
    fx_set_bg(FX_WINDOW_BG);
    {   /* v2.4: 控件层抗锯齿档位可由环境变量覆盖, 便于"同页同帧"对比截图(0=关, 1=SDF, 2=+图元羽化) */
        const char *aa = getenv("FXTK_AA");
        if (aa) fx_set_widget_aa(atoi(aa));
    }
    /* 键鼠页已有坐标监视; 不叠加高频 T: 调试文本, 避免滚动页污染画面/文字缓存 */
    fx_set_touch_debug(0);
    {   /* 标题统一取 FXTK_VERSION, 不再手写版本号 (此前写着 v2.2 已过时) */
        static char _title[64];
        snprintf(_title, sizeof(_title), "fxtk v%s · demo (%s)", FXTK_VERSION,
                 fx_backend_name());
        fx_set_window_title(_title);
    }
    build_ui();
    fx_canvas_new(pixel("0,271", "0,271"), name("fixer"), anim(1),
                  color(FX_RGB(240,240,240)), call(on_fix));
}
