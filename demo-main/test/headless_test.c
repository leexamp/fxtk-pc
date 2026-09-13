/**
 * headless_test.c — 无窗口单元测试
 *
 * 用假驱动的 fx_driver_t 直接驱动核心库, 不初始化 SDL/窗口/字体:
 * 只验证布局(pixel/percent/grid)、命中测试(press/release)、控件生命周期,
 * 以及 value 读写。文字/纹理在无字体时为 no-op, 不影响布局结果。
 *
 * 构建: cd demo-main && make test
 * 手动等价:
 *   gcc -I. -I../components/fxtk \
 *       ../components/fxtk/fxtk.c ../components/fxtk/fxtk_draw.c \
 *       ../components/fxtk/fxtk_widgets.c ../components/fxtk/fxtk_effects.c \
 *       ../components/fxtk/fxtk_extra.c ../components/fxtk/fxtk_backends.c \
 *       test/headless_test.c -o test/headless_test -lm
 *   ./test/headless_test
 * 另有一份 -DFXTK_BACKEND_STUB 变体 (make test 会一并运行)。
 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include "fxtk_image.h"
#include "fxtk_backends.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* ---------- 假驱动 ---------- */
static int s_hit_cb_ok = 0;          /* 命中回调用触发器 */

static int drv_init(void) { return 0; }
static void drv_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {(void)x0;(void)y0;(void)x1;(void)y1;}
static void drv_push_pixels(const uint32_t *px, uint32_t n) {(void)px;(void)n;}
static void drv_hold_begin(void) {}
static void drv_hold_end(void) {}
static void drv_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t c) {(void)x0;(void)y0;(void)x1;(void)y1;(void)c;}
static int  drv_touch_read(int *x, int *y, int *pressed) {(void)x;(void)y;(void)pressed; return 0;}
static int  drv_key_read(fx_keyev_t *ev) {(void)ev; return 0;}
static void drv_clip_set(const char *s) {(void)s;}
static const char *drv_clip_get(void) { return ""; }
static void drv_set_title(const char *s) {(void)s;}
static int  drv_wheel_read(int *x, int *y, int *dy) {(void)x;(void)y;(void)dy; return 0;}
static void drv_blit_img(const uint32_t *px,int w,int h,int dx,int dy,int dw,int dh,int dark) {(void)px;(void)w;(void)h;(void)dx;(void)dy;(void)dw;(void)dh;(void)dark;}
static void drv_set_clip_rect(int x1,int y1,int x2,int y2) {(void)x1;(void)y1;(void)x2;(void)y2;}
static void drv_fill_tri(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t c) {(void)x1;(void)y1;(void)x2;(void)y2;(void)x3;(void)y3;(void)c;}
static void drv_draw_line(int x1,int y1,int x2,int y2,uint32_t c) {(void)x1;(void)y1;(void)x2;(void)y2;(void)c;}
static void drv_blit_tex(void *t,int sx,int sy,int sw,int sh,int dx,int dy) {(void)t;(void)sx;(void)sy;(void)sw;(void)sh;(void)dx;(void)dy;}
static void drv_blit_img_rot(const uint32_t *px,int w,int h,int cx,int cy,int dw,int dh,double ang) {(void)px;(void)w;(void)h;(void)cx;(void)cy;(void)dw;(void)dh;(void)ang;}

static fx_driver_t s_drv = {
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
    .blit_img = drv_blit_img,
    .set_clip_rect = drv_set_clip_rect,
    .fill_tri = drv_fill_tri,
    .draw_line = drv_draw_line,
    .blit_tex = drv_blit_tex,
    .blit_img_rot = drv_blit_img_rot,
};

/* ---------- 文字/纹理桩 (无 SDL/字体, 均为 no-op) ---------- */
void fx_draw_text(int x, int y, const char *s) {(void)x;(void)y;(void)s;}
void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg) {(void)x;(void)y;(void)s;(void)fg;(void)bg;}
void fx_draw_text_c_n(int x, int y, const char *s, int n, fx_color_t fg, fx_color_t bg) {(void)x;(void)y;(void)s;(void)n;(void)fg;(void)bg;}
int  fx_text_width(const char *s) {(void)s; return 0;}
int  fx_text_width_n(const char *s, int n) {(void)s;(void)n; return 0;}
void fxtk_draw_text_size(int size, int x, int y, const char *t, fx_color_t fg, fx_color_t bg) {(void)size;(void)x;(void)y;(void)t;(void)fg;(void)bg;}
int  fxtk_text_width_size(int size, const char *t) {(void)size;(void)t; return 0;}
void *fxtk_font_size(int size) {(void)size; return NULL;}
int  fxtk_font_height(int size) {(void)size; return 0;}
void fxtk_font_set_size(int size) {(void)size;}

/* ---------- SDL 驱动才有的符号桩 ---------- */
int  fxtk_fps(void) { return 60; }
int  fxtk_shift_down(void) { return 0; }
int  fxtk_right_click(int *x, int *y) {(void)x;(void)y; return 0;}
int  fxtk_ui_scale(void) { return 100; }   /* 480 设计宽=100 */

/* ---------- 测试回调 ---------- */
static void on_hit(fx_widget_t *w, void *ud) { (void)w; (void)ud; s_hit_cb_ok = 1; }
static int s_sc_hit = 0;
static void on_sc_hit(fx_widget_t *w, void *ud) { (void)w; (void)ud; s_sc_hit = 1; }

static void check_rect(const char *what, fx_widget_t *w, int x1,int y1,int x2,int y2) {
    int ax1=-1,ay1=-1,ax2=-1,ay2=-1;
    fx_widget_rect(w,&ax1,&ay1,&ax2,&ay2);
    if (ax1!=x1||ay1!=y1||ax2!=x2||ay2!=y2) {
        printf("  [FAIL] %s: got (%d,%d,%d,%d) want (%d,%d,%d,%d)\n",
               what, ax1,ay1,ax2,ay2, x1,y1,x2,y2);
        assert(0);
    }
    printf("  [ok]   %s = (%d,%d,%d,%d)\n", what, ax1,ay1,ax2,ay2);
}

int main(void) {
    int fails = 0;
    fx_init(&s_drv);

    printf("== fxtk headless test ==\n");

    /* 1. pixel 布局 */
    printf("[1] pixel 绝对定位\n");
    fx_button_new(pixel("10,10","100,40"), name("b1"), title("A"));
    fx_layout();
    check_rect("b1 pixel", fx_find("b1"), 10,10,100,40);

    /* 2. percent 布局 (480x272): 25%~75% 应得 (120,68,359,203) */
    printf("[2] percent 响应式\n");
    fx_label_new(percent("0.25,0.25","0.75,0.75"), name("l1"), title("B"));
    fx_layout();
    check_rect("l1 percent", fx_find("l1"), 120,68,359,203);

    /* 3. grid 布局: 3x3 网格 (0,0)-(300,180), 单元格 100x60 */
    printf("[3] grid 网格\n");
    fx_grid_map(pixel("0,0","300,180"), line(3), row(3), name("g"));
    fx_button_new(grid("g",2,2,2,2), name("gbtn"), title("X"));
    fx_layout();
    /* 单元(2,2): x1=0+(2-1)*100=100, y1=0+(2-1)*60=60, x2=0+2*100-1=199, y2=0+2*60-1=119 */
    check_rect("gbtn grid", fx_find("gbtn"), 100,60,199,119);

    /* 4. 命中测试: 点击按钮内应触发回调, 点击外面不应 */
    printf("[4] 命中测试 press/release\n");
    fx_button_new(pixel("200,200","280,240"), name("hit"), title("Hit"), call(on_hit));
    fx_layout();
    s_hit_cb_ok = 0;
    fx_touch_press(220,220);   /* 命中 */
    fx_touch_release(220,220);
    if (!s_hit_cb_ok) { printf("  [FAIL] 命中区域内未触发回调\n"); fails++; }
    else printf("  [ok]   命中回调触发\n");
    s_hit_cb_ok = 0;
    fx_touch_press(10,10);     /* 未命中 */
    fx_touch_release(10,10);
    if (s_hit_cb_ok) { printf("  [FAIL] 命中区域外却触发了回调\n"); fails++; }
    else printf("  [ok]   区域外未触发\n");

    /* 5. value 读写 */
    printf("[5] value 读写\n");
    fx_slider_new(pixel("10,100","200,116"), name("s1"));
    fx_layout();
    fx_set_value(fx_find("s1"), 42);
    if (fx_get_value(fx_find("s1")) != 42) { printf("  [FAIL] set/get value\n"); fails++; }
    else printf("  [ok]   value=42\n");

    /* 6. 控件计数 */
    printf("[6] 控件计数\n");
    int n0 = fxtk_widget_count();
    fx_button_new(pixel("10,250","50,270"), name("extra"), title("E"));
    fx_layout();
    int n1 = fxtk_widget_count();
    if (n1 != n0 + 1) { printf("  [FAIL] 控件计数 %d -> %d\n", n0, n1); fails++; }
    else printf("  [ok]   count %d -> %d\n", n0, n1);

    /* 7. 查找与删除 (fx_delete 用 fx_wptr 或 grid 定位, 不是 name) */
    printf("[7] fx_find / fx_delete\n");
    if (!fx_find("extra")) { printf("  [FAIL] fx_find 未找到 extra\n"); fails++; }
    else {
        fx_widget_t *e = fx_find("extra");
        fx_delete(fx_wptr(e));
        if (fx_find("extra")) { printf("  [FAIL] fx_delete 后仍能找到\n"); fails++; }
        else printf("  [ok]   find+delete 正常\n");
    }

    /* 8. 画布便捷 API: fx_canvas_size + fx_canvas_clear (v2.2 新增) */
    printf("[8] 画布便捷 API\n");
    fx_canvas_new(pixel("10,10","100,50"), name("cvx"));
    fx_layout();
    int cw = 0, ch = 0;
    fx_canvas_size(fx_find("cvx"), &cw, &ch);
    if (cw != 91 || ch != 41) { printf("  [FAIL] canvas size=%dx%d want 91x41\n", cw, ch); fails++; }
    else printf("  [ok]   canvas size %dx%d\n", cw, ch);
    fx_canvas_clear(fx_find("cvx"), FX_WINDOW_BG);   /* 仅要求不崩溃 */
    printf("  [ok]   canvas_clear 无异常\n");

    /* 9. v2.3.1 回归: fx_image_create 尺寸上限 (旧: int16 截断 → fx_draw_image_ex 越界读 SEGV) */
    printf("[9] fx_image_create 边界\n");
    {
        fx_image_t *bad = fx_image_create(40000, 10);
        fx_image_t *ok  = fx_image_create(320, 240);
        if (bad) { printf("  [FAIL] 40000x10 应被拒绝\n"); fails++; fx_image_free(bad); }
        else printf("  [ok]   超大图像已拒绝\n");
        if (!ok) { printf("  [FAIL] 320x240 应创建成功\n"); fails++; }
        else { fx_image_free(ok); printf("  [ok]   正常尺寸创建成功\n"); }
    }

    /* 10. v2.3.1 回归: SCROLL 命中分支的子件可见性 (旧: 隐藏子件仍可被点击/触发回调) */
    printf("[10] SCROLL 命中: 隐藏子件不可点\n");
    {
        s_sc_hit = 0;
        fx_scroll_new(pixel("10,10","200,200"), name("sc1"));
        fx_button_new(pixel("20,20","80,60"), name("scbtn"), title("S"), call(on_sc_hit));
        fx_layout();
        fx_set_visible(fx_find("scbtn"), 0);
        fx_layout();
        fx_touch_press(50,40); fx_touch_release(50,40);
        if (s_sc_hit) { printf("  [FAIL] 隐藏子件仍被命中\n"); fails++; }
        else printf("  [ok]   隐藏子件不可点\n");
        fx_set_visible(fx_find("scbtn"), 1);
        fx_layout();
        fx_touch_press(50,40); fx_touch_release(50,40);
        if (!s_sc_hit) { printf("  [FAIL] 可见子件未命中(正例失效)\n"); fails++; }
        else printf("  [ok]   可见子件正常命中\n");
    }

    /* 11. v2.3.1 回归: grid 晚于子件创建 (旧: 引用只在创建瞬间解析, 子件永远 (0,0,0,0)) */
    printf("[11] grid 晚绑定\n");
    {
        fx_button_new(grid("g_late",1,1,1,1), name("late"), title("L"));
        fx_grid_map(pixel("0,100","300,220"), line(2), row(2), name("g_late"));
        fx_layout();
        int a1=-1,b1=-1,a2=-1,b2=-1;
        fx_widget_rect(fx_find("late"),&a1,&b1,&a2,&b2);
        /* 网格 (0,100)-(300,220) 2x2: 单元(1,1) = (0,100)-(149,159) */
        if (a1!=0||b1!=100||a2!=149||b2!=159) {
            printf("  [FAIL] 晚绑定未解析: (%d,%d,%d,%d) want (0,100,149,159)\n",a1,b1,a2,b2); fails++;
        } else printf("  [ok]   晚绑定解析成功 (0,100,149,159)\n");
    }

    /* 12. v2.3.1 回归: percent 误写 "100,100" 不再 int16 回绕 (旧: 坐标变负, 控件甩出屏幕) */
    printf("[12] percent 钳制\n");
    {
        fx_label_new(percent("100,100","100,100"), name("pct_bad"), title("P"));
        fx_layout();
        int a1=-1,b1=-1,a2=-1,b2=-1;
        fx_widget_rect(fx_find("pct_bad"),&a1,&b1,&a2,&b2);
        if (a1 < 0 || b1 < 0) { printf("  [FAIL] 回绕: (%d,%d,%d,%d)\n",a1,b1,a2,b2); fails++; }
        else printf("  [ok]   无回绕 (%d,%d,%d,%d)\n", a1,b1,a2,b2);
    }

    /* 13. v2.4 后端服务层: 随机可复现 / 路径 / 文件 / 图片 / 偏好 / 时间 */
    printf("[13] fx_backends 服务层 (%s)\n", fx_backend_name());
    {
        /* --- 随机: 同种子同序列 (测试与 demo 可复现的基石) --- */
        uint32_t a[4], b[4];
        fx_rand_seed(42); for (int i = 0; i < 4; i++) a[i] = fx_rand_u32();
        fx_rand_seed(42); for (int i = 0; i < 4; i++) b[i] = fx_rand_u32();
        if (a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3]) printf("  [ok]   PCG32 同种子可复现\n");
        else { printf("  [FAIL] 同种子序列不一致\n"); fails++; }
        fx_rand_seed(43); uint32_t c0 = fx_rand_u32();
        if (c0 != a[0]) printf("  [ok]   不同种子序列不同\n");
        else { printf("  [FAIL] 换种子后首值相同\n"); fails++; }
        int rng_ok = 1;
        for (int i = 0; i < 2000; i++) { int v = fx_rand_range(5, 10); if (v < 5 || v > 10) rng_ok = 0; }
        for (int i = 0; i < 2000; i++) { uint32_t v = fx_rand_below(7); if (v >= 7) rng_ok = 0; }
        for (int i = 0; i < 1000; i++) { float f = fx_randf(); if (f < 0.0f || f >= 1.0f) rng_ok = 0; }
        if (rng_ok) printf("  [ok]   rand_range/rand_below/randf 边界正确\n");
        else { printf("  [FAIL] 随机范围越界\n"); fails++; }

        /* --- 路径工具 --- */
        char p[256];
        int pok = 1;
        fx_path_join(p, sizeof p, "/a/b", "c.txt");   if (strcmp(p, "/a/b/c.txt")) pok = 0;
        fx_path_join(p, sizeof p, "/a/b/", "c.txt");  if (strcmp(p, "/a/b/c.txt")) pok = 0;
        fx_path_join(p, sizeof p, "", "c.txt");       if (strcmp(p, "c.txt")) pok = 0;
        if (strcmp(fx_path_basename("/a/b/c.png"), "c.png")) pok = 0;
        if (strcmp(fx_path_ext("/a/b/c.png"), ".png")) pok = 0;
        if (strcmp(fx_path_ext("/a/b/c"), "")) pok = 0;
        fx_path_dir(p, sizeof p, "/a/b/c.png");       if (strcmp(p, "/a/b")) pok = 0;
        if (pok) printf("  [ok]   路径工具 join/basename/ext/dir\n");
        else { printf("  [FAIL] 路径工具\n"); fails++; }

        /* --- 文件读写往返 + 追加 --- */
        const char *td = getenv("FXTK_TEST_DIR");
        if (!td || !td[0]) td = "/tmp";
        char fp[320];
        fx_path_join(fp, sizeof fp, td, "fxtk_backend_test.bin");
        const char payload[] = "fxtk-backends-往返";
        int wok = fx_file_write(fp, payload, (int)sizeof(payload), 0);
        int sz = 0;
        char *back = (char *)fx_file_read(fp, &sz);
        int rok = back && sz == (int)sizeof(payload) && memcmp(back, payload, (size_t)sz) == 0;
        fx_file_free(back);
        if (wok && rok && fx_file_exists(fp)) printf("  [ok]   文件写读往返 (%d 字节)\n", sz);
        else { printf("  [FAIL] 文件写读往返 (w=%d r=%d)\n", wok, rok); fails++; }
        fx_file_write(fp, "++", 2, 1);
        long long fsz = 0; fx_file_size(fp, &fsz);
        if (fsz == (long long)sizeof(payload) + 2) printf("  [ok]   追加写与文件大小\n");
        else { printf("  [FAIL] 追加/大小 (=%lld)\n", fsz); fails++; }

        /* --- 图片: PNG 存/取往返 (走 vendored stb) --- */
        unsigned char img[4 * 3 * 4];
        for (int i = 0; i < 4 * 3; i++) {
            img[i*4+0] = (unsigned char)(i * 20); img[i*4+1] = 128;
            img[i*4+2] = (unsigned char)(255 - i * 20); img[i*4+3] = 255;
        }
        char ip[320];
        fx_path_join(ip, sizeof ip, td, "fxtk_backend_test.png");
        if (fx_img_save_png(ip, img, 4, 3, 4)) {
            fx_img_t im;
            int lok = fx_img_load_file(ip, &im);
            if (lok && im.w == 4 && im.h == 3 && im.channels == 4 &&
                im.pixels[0] == img[0] && im.pixels[1] == img[1] && im.pixels[2] == img[2]) {
                printf("  [ok]   PNG 存/取往返 (4x3, %d 通道)\n", im.channels);
            } else { printf("  [FAIL] PNG 往返 (load=%d)\n", lok); fails++; }
            if (lok) fx_img_free(&im);
            if (fx_img_has_file_ext(ip)) printf("  [ok]   图片后缀识别\n");
            else { printf("  [FAIL] 后缀识别\n"); fails++; }
            if (!fx_img_has_file_ext("x.txt")) printf("  [ok]   非图片后缀被拒\n");
            else { printf("  [FAIL] 非图片后缀误判\n"); fails++; }
        } else { printf("  [FAIL] PNG 写入\n"); fails++; }

        /* --- 偏好往返与持久化 --- */
        fx_prefs_set("theme", "dark");
        char v[64];
        if (fx_prefs_get("theme", v, sizeof v) == 4 && strcmp(v, "dark") == 0) printf("  [ok]   偏好读写\n");
        else { printf("  [FAIL] 偏好读写\n"); fails++; }
        if (fx_prefs_save()) {
            fx_prefs_set("theme", "light");
            fx_prefs_load();
            fx_prefs_get("theme", v, sizeof v);
            if (strcmp(v, "dark") == 0) printf("  [ok]   偏好持久化往返\n");
            else { printf("  [FAIL] 偏好持久化 (=%s)\n", v); fails++; }
        } else printf("  [note] 偏好落盘跳过 (目录不可写)\n");

        /* --- 能力协商 / 系统 / 时间 --- */
        fx_caps_t caps = fx_backend_caps();
        if (caps.platform && caps.max_texture > 0) {
            printf("  [ok]   能力: %s gpu=%d aa=%d quad=%d clip=%d maxtex=%d\n",
                   caps.platform, caps.has_gpu, caps.gpu_aa, caps.quad_warp, caps.clipboard, caps.max_texture);
        } else { printf("  [FAIL] 能力查询\n"); fails++; }
        if (fx_cpu_count() >= 1) printf("  [ok]   cpu_count=%d\n", fx_cpu_count());
        else { printf("  [FAIL] cpu_count\n"); fails++; }
        uint64_t t0 = fx_time_ms();
        fx_sleep_ms(2);
        uint64_t t1 = fx_time_ms();
        if (t1 >= t0) printf("  [ok]   时间单调 (%llu → %llu ms)\n", (unsigned long long)t0, (unsigned long long)t1);
        else { printf("  [FAIL] 时间非单调\n"); fails++; }

        /* --- 截图: 无 read_pixels 钩子的驱动必须优雅失败, 不得崩溃 --- */
        int sc = fx_screenshot("/tmp/fxtk_should_not_exist.png");
        if (sc == 0) printf("  [ok]   无 read_pixels 钩子时 fx_screenshot 优雅返回 0\n");
        else { printf("  [FAIL] 假驱动不该能截图\n"); fails++; }
        if (fx_file_exists("/tmp/fxtk_should_not_exist.png")) { printf("  [FAIL] 失败路径却写了文件\n"); fails++; }
    }

    /* 14. v2.4 四边形形变: 单应矩阵与逆矩阵 (数学层, 不依赖像素) */
    printf("[14] 四边形形变 (单应)\n");
    {
        const float unit[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
        float quad[8] = { 120.0f, 40.0f, 360.0f, 40.0f, 440.0f, 200.0f, 40.0f, 200.0f };  /* 梯形 */
        float H[9];
        if (!fx_quad_homography(unit, quad, H)) { printf("  [FAIL] 单应求解失败\n"); fails++; }
        else {
            int ok = 1;
            for (int i = 0; i < 4; i++) {
                float x = unit[i * 2], y = unit[i * 2 + 1];
                float w = H[6] * x + H[7] * y + H[8];
                float dx = (H[0] * x + H[1] * y + H[2]) / w;
                float dy = (H[3] * x + H[4] * y + H[5]) / w;
                if (fabsf(dx - quad[i * 2]) > 0.01f || fabsf(dy - quad[i * 2 + 1]) > 0.01f) ok = 0;
            }
            if (ok) printf("  [ok]   四角精确映射 (误差 < 0.01px)\n");
            else { printf("  [FAIL] 角点映射不准\n"); fails++; }
            float Hi[9];
            if (fx_mat3_invert(H, Hi)) {
                int ok2 = 1;
                for (int i = 0; i < 4; i++) {
                    float px = quad[i * 2], py = quad[i * 2 + 1];
                    float w = Hi[6] * px + Hi[7] * py + Hi[8];
                    float u = (Hi[0] * px + Hi[1] * py + Hi[2]) / w;
                    float v = (Hi[3] * px + Hi[4] * py + Hi[5]) / w;
                    if (fabsf(u - unit[i * 2]) > 0.01f || fabsf(v - unit[i * 2 + 1]) > 0.01f) ok2 = 0;
                }
                if (ok2) printf("  [ok]   逆单应回代 (四边形 → 单位方)\n");
                else { printf("  [FAIL] 逆单应不准\n"); fails++; }
            } else { printf("  [FAIL] 3x3 求逆失败\n"); fails++; }
        }
        float degen[8] = { 10, 10, 20, 10, 30, 10, 40, 10 };   /* 共线 = 零面积 */
        float Hd[9];
        if (!fx_quad_homography(unit, degen, Hd)) printf("  [ok]   退化四边形被拒绝\n");
        else { printf("  [FAIL] 退化四边形竟求解成功\n"); fails++; }
        /* 14b. v2.4 P4: 四角透视权重 (GPU 真透视用) */
        {
            float rect[8] = { 0, 0, 100, 0, 100, 60, 0, 60 };          /* 无透视: 权重应全为 1 */
            float rect_w[4];
            int ok = fx_quad_corner_weights(rect, rect_w);
            int all1 = ok && fabsf(rect_w[0]-1)<1e-4f && fabsf(rect_w[1]-1)<1e-4f &&
                              fabsf(rect_w[2]-1)<1e-4f && fabsf(rect_w[3]-1)<1e-4f;
            printf("  [%s] 矩形(无透视)权重恒为 1\n", all1 ? "ok" : "FAIL"); if (!all1) fails++;

            /* 梯形(上边收窄 → 有透视): 权重须使【透视插值】与 CPU 单应结果一致。
             * 取四边形四角坐标平均点 C(屏幕空间), CPU 用 H⁻¹ 求 uv; 另用四角权重做
             * 透视校正插值(uv = Σλ·uv_i/d_i ÷ Σλ/d_i, λ=1/4) —— 两者应吻合。 */
            float trap[8] = { 25, 0, 75, 0, 100, 60, 0, 60 };
            float w4[4]; float H2[9], Hi2[9];
            int ok2 = fx_quad_corner_weights(trap, w4) && fx_quad_homography(unit, trap, H2) && fx_mat3_invert(H2, Hi2);
            float cx = (trap[0]+trap[2]+trap[4]+trap[6])*0.25f;
            float cy = (trap[1]+trap[3]+trap[5]+trap[7])*0.25f;
            float uu = Hi2[0]*cx + Hi2[1]*cy + Hi2[2];
            float vv = Hi2[3]*cx + Hi2[4]*cy + Hi2[5];
            float ww = Hi2[6]*cx + Hi2[7]*cy + Hi2[8];
            uu /= ww; vv /= ww;
            const float cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
            float su = 0, sv = 0, sw = 0;
            for (int i = 0; i < 4; i++) { float l = 0.25f / w4[i]; su += l*cu[i]; sv += l*cv[i]; sw += l; }
            su /= sw; sv /= sw;
            int near_ok = ok2 && fabsf(su-uu) < 2e-3f && fabsf(sv-vv) < 2e-3f;
            printf("  [%s] 梯形透视权重: 插值 uv=(%.4f,%.4f) vs 单应 uv=(%.4f,%.4f)\n",
                   near_ok ? "ok" : "FAIL", su, sv, uu, vv);
            if (!near_ok) fails++;
        }
    }

    /* 15. v2.4 canvas 变换栈 (2D 仿射) */
    printf("[15] canvas 变换栈\n");
    {
        float x = 0, y = 0;
        fx_canvas_transform_point(10, 20, &x, &y);
        if (x == 10 && y == 20) printf("  [ok]   栈空 = 恒等\n");
        else { printf("  [FAIL] 空栈应恒等 (%g,%g)\n", x, y); fails++; }
        fx_canvas_push_affine(1, 0, 0, 1, 5, 7);          /* 平移 (5,7) */
        fx_canvas_transform_point(10, 20, &x, &y);
        if (x == 15 && y == 27) printf("  [ok]   平移 (10,20)→(15,27)\n");
        else { printf("  [FAIL] 平移 (%g,%g)\n", x, y); fails++; }
        fx_canvas_push_affine(2, 0, 0, 2, 0, 0);          /* 后 push 在外层: 先平移后缩放 */
        fx_canvas_transform_point(10, 20, &x, &y);
        if (x == 30 && y == 54) printf("  [ok]   复合语义 (后 push 在外层): (10,20)→(30,54)\n");
        else { printf("  [FAIL] 复合 (%g,%g)\n", x, y); fails++; }
        if (fx_transform_depth() == 2) printf("  [ok]   栈深 = 2\n");
        else { printf("  [FAIL] 栈深 %d\n", fx_transform_depth()); fails++; }
        fx_canvas_pop_affine(); fx_canvas_pop_affine();
        if (fx_transform_depth() == 0 && (fx_canvas_transform_point(3, 4, &x, &y), x == 3 && y == 4))
            printf("  [ok]   pop 到底后恢复恒等\n");
        else { printf("  [FAIL] pop\n"); fails++; }
        fx_canvas_push_affine(0, 1, -1, 0, 0, 0);         /* 旋转 90°: (1,0)→(0,1) */
        fx_canvas_transform_point(1, 0, &x, &y);
        if (fabsf(x) < 0.001f && fabsf(y - 1) < 0.001f) printf("  [ok]   旋转 90°\n");
        else { printf("  [FAIL] 旋转 (%g,%g)\n", x, y); fails++; }
        fx_canvas_pop_affine();
        for (int i = 0; i < 12; i++) fx_canvas_push_affine(1, 0, 0, 1, 1, 1);   /* 溢出保护 */
        if (fx_transform_depth() == 8) printf("  [ok]   栈满封顶 8 (覆盖栈顶不崩)\n");
        else { printf("  [FAIL] 溢出后栈深 %d\n", fx_transform_depth()); fails++; }
        fx_transform_reset();
        if (fx_transform_depth() == 0) printf("  [ok]   reset\n");
        else { printf("  [FAIL] reset\n"); fails++; }
    }

    printf("== done: %s (%d fail) ==\n", fails ? "FAIL" : "PASS", fails);    return fails ? 1 : 0;
}
