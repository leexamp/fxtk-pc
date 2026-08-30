#include <stdlib.h>
/**
 * app.c — fxtk 综合演示 (5 页: 波形/动效/控件/图片/3D光追)
 */
#include "fxtk.h"
#include "fxtk_image.h"
#include "fxtk_effects.h"
#include "raymarch.h"
#include "gpu_raymarch.h"
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
static int s_gfx_t = 0, s_spin = 30, s_gfx_mode = 0;
#define TRAIL_N 14
static int s_tx[TRAIL_N], s_ty[TRAIL_N], s_ti = 0;
/* 3D 页状态 */
static fx_image_t *s_rt = NULL;
static int s_oc = 0;
static int s_gpu = 0;                 /* 超频: 原生分辨率光追 */
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
    s_pic = (s_pic + 1) % 3;
    fx_set_image(w, s_pics[s_pic]);
    char buf[48];
    snprintf(buf, sizeof(buf), "图案 %d / 3 (点击切换)", s_pic + 1);
    fx_set_title(fx_find("img_info"), buf);
}
static void on_zoom(fx_widget_t *w, void *ud) {
    fx_image_set_zoom(fx_find("pic"), 10 + fx_get_value(w) * 3);
}
static void on_spin(fx_widget_t *w, void *ud) { s_spin = fx_get_value(w); }
static void on_mode(fx_widget_t *w, void *ud) {
    s_gfx_mode = (s_gfx_mode + 1) % 3;
    const char *m[3] = { "模式: 全部", "模式: 贴图", "模式: 矢量" };
    fx_set_title(fx_find("gfx_info"), m[s_gfx_mode]);
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
            fx_draw_line(x - 1, prev_y, x, y);
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
static void on_gfx(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
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
}
static void on_3d(fx_widget_t *w, void *ud) {
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    int rw = s_oc ? cw : (cw > 640 ? 640 : cw);
    int rh = s_oc ? ch : (ch > 360 ? 360 : ch);
    if (rw < 16) rw = 16; if (rh < 16) rh = 16;
    if (!s_rt || s_rt->w != rw || s_rt->h != rh) {
        if (s_rt) fx_image_free(s_rt);
        s_rt = fx_image_create(rw, rh);
        if (!s_rt) return;
    }
    if (s_gpu && gpu_raymarch_ok()) {
        gpu_raymarch_render(s_rt->px, rw, rh, s_rt_time);
    } else {
        raymarch_render(s_rt->px, rw, rh, s_rt_time, 24 + s_rqual);
    }
    fx_set_color(FX_BLACK); fx_fill_rect(0, 0, cw - 1, ch - 1);
    fx_draw_image(s_rt, 0, 0, cw, ch);
    s_rt_time += 0.016f * (0.3f + s_rspeed * 0.06f);

    /* FPS 统计 (每 500ms 刷新) */
    s_frames++;
    long now = now_ms();
    if (!s_fps_t0) s_fps_t0 = now;
    if (now - s_fps_t0 >= 500) {
        char buf[48];
        snprintf(buf, sizeof(buf), "FPS: %d (%dx%d) [%s]",
                 (int)(s_frames * 1000L / (now - s_fps_t0)), rw, rh,
                 (s_gpu && gpu_raymarch_ok()) ? gpu_raymarch_renderer() : "CPU");
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

    fx_grid_map(pixel("6,32", "280,220"), line(3), row(3), name("keys"), page(2), dense());
    for (int i = 0; i < 9; i++) {
        fx_widget_t *b = fx_button_new(grid("keys", i / 3 + 1, i % 3 + 1, i / 3 + 1, i % 3 + 1),
                                       title(s_keys[i]), color(FX_RGB(33, 150, 243)), page(2));
        fx_set_cb(b, on_key, (void *)(intptr_t)i);
    }
    fx_button_new(pixel("292,92", "444,142"), page(2), title("重置"), color(FX_RGB(244, 67, 54)), call(on_reset));
    fx_label_new(pixel("292,152", "444,220"), name("info"), page(2), title("点击数字键试试"), fgcolor(FX_RGB(51, 51, 51)));

    fx_image_new(pixel("6,32", "280,220"), name("pic"), page(3), image(s_pics[0]), call(on_img));
    fx_label_new(pixel("292,40", "444,58"), page(3), title("缩放 (拖动试试)"), fgcolor(FX_RGB(51, 51, 51)));
    fx_slider_new(pixel("292,64", "444,84"), name("zoom"), page(3), value(30), color(FX_RGB(33, 150, 243)), call(on_zoom));
    fx_label_new(pixel("292,100", "444,220"), name("img_info"), page(3), title("图案 1 / 3 (点击切换)"), fgcolor(FX_RGB(51, 51, 51)));

    /* 页5: 3D 光线步进 */
    fx_canvas_new(pixel("6,32", "444,196"), name("rt_cv"), page(4), anim(1), color(FX_BLACK), call(on_3d));
    fx_canvas_set_buf(fx_find("rt_cv"), 0);   /* 3D 直绘, 放大不空白 */
    fx_canvas_new(pixel("6,32", "444,196"), name("pt_cv"), page(11), anim(1), color(FX_BLACK), call(on_pt));
    fx_slider_new(pixel("6,204", "300,220"), page(11), call(on_pt_slider));
    fx_label_new(pixel("306,204", "444,220"), page(11), title("拖动调粒子数(×200)"), fgcolor(FX_RGB(51, 51, 51)));
    fx_button_new(pixel("6,202", "76,236"), page(4), title("超频: 关"), color(FX_RGB(244, 67, 54)), call(on_oc));
    fx_button_new(pixel("82,202", "152,236"), page(4), title("GPU: 关"), color(FX_RGB(76, 175, 80)), call(on_gpu));
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
#define PT_MAX 2000000
typedef struct { float x,y,vx,vy; uint8_t c; } pt_t;
static pt_t s_pt[PT_MAX];
static int s_pt_init = 0; static int s_pt_n = 6000;
static fx_image_t *s_ptimg=NULL; static int s_ptimg_w=0, s_ptimg_h=0;
static double s_pt_last = 0; static float s_pt_fps = 0;
static double pt_now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1e6; }
static void on_pt_slider(fx_widget_t *w, void *ud){ (void)ud; int v=fx_get_value(w); s_pt_n=v*20000; if(s_pt_n<100)s_pt_n=100; if(s_pt_n>PT_MAX)s_pt_n=PT_MAX; }
static void on_pt(fx_widget_t *w, void *ud){
    (void)ud; int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    if(!s_pt_init){ srand(12345);
        for(int i=0;i<PT_MAX;i++){ s_pt[i].x=(float)(rand()%cw); s_pt[i].y=(float)(rand()%ch);
            float a=(float)(rand()%360)*0.01745f, sp=0.5f+(float)(rand()%100)/100.0f;
            s_pt[i].vx=cosf(a)*sp; s_pt[i].vy=sinf(a)*sp; s_pt[i].c=(uint8_t)(rand()%6); }
        s_pt_init=1; s_pt_last=pt_now(); }
    int hw=cw/2, hh=ch/2; if(hw<1)hw=1; if(hh<1)hh=1;
    if(!s_ptimg || s_ptimg_w!=hw || s_ptimg_h!=hh){ s_ptimg=fx_image_create(hw,hh); s_ptimg_w=hw; s_ptimg_h=hh; }
    if(!s_ptimg) return;
    double now=pt_now(); double dt=now-s_pt_last; s_pt_last=now; if(dt<0.1)dt=0.1; if(dt>50)dt=50;
    s_pt_fps=s_pt_fps*0.9f+(float)(1000.0/dt)*0.1f;
    uint32_t *px = s_ptimg->px;
    memset(px, 0, (size_t)hw*hh*sizeof(uint32_t));
    static const uint32_t cols[6]={FX_RED,FX_GREEN,FX_BLUE,FX_YELLOW,FX_CYAN,FX_MAGENTA};
    for(int i=0;i<s_pt_n;i++){ pt_t*p=&s_pt[i];
        p->x+=p->vx*(float)dt*0.06f; p->y+=p->vy*(float)dt*0.06f;
        if(p->x<0)p->x+=(float)cw; if(p->x>=cw)p->x-=(float)cw;
        if(p->y<0)p->y+=(float)ch; if(p->y>=ch)p->y-=(float)ch;
        int X=(int)p->x>>1, Y=(int)p->y>>1;
        if(X>=0&&X<hw&&Y>=0&&Y<hh) px[Y*hw+X]=cols[p->c]; }
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
    /* 键鼠页已有坐标监视; 不叠加高频 T: 调试文本, 避免滚动页污染画面/文字缓存 */
    fx_set_touch_debug(0);
    fx_set_window_title("demo v2.2");
    build_ui();
    fx_canvas_new(pixel("0,271", "0,271"), name("fixer"), anim(1),
                  color(FX_RGB(240,240,240)), call(on_fix));
}
