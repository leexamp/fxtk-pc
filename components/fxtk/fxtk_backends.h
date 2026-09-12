/**
 * fxtk_backends.h — v2.4 通用跨平台服务层
 *
 * 把"平台相关、但每个应用都要用"的能力收在一处:
 *   随机 / 时间 / 路径 / 文件 / 目录 / 图片 / 剪贴板 / 系统信息 / 日志 / 偏好 / 能力协商
 * 取代原 fxtk_fs.h(fxtk_fs.h 保留为兼容薄壳, 一个版本后移除)。
 *
 * 实现选择 (编译期):
 *   -DFXTK_BACKEND_STUB  —— 无头测试: 确定性时钟 + 固定种子随机 + 内存文件系统
 *   _WIN32               —— Win32 实现
 *   ESP_PLATFORM         —— ESP-IDF 实现
 *   其它                 —— POSIX 实现 (Linux/macOS)
 *
 * 设计原则
 *   1. 不引入新的链接依赖: 屏幕尺寸 / DPI 由平台驱动通过 fx_backend_set_screen() 注入,
 *      本模块自己不开 X11/GL 连接(否则无头测试也得链 -lX11)。
 *   2. 随机数是自带的 PCG32, 不依赖 rand(); 同种子必然同序列 → 测试与 demo 可复现。
 *   3. 所有写缓冲区的接口都带容量, 越界一律截断并返回实际长度; 失败返回 <=0。
 *   4. 图片编解码用 vendored stb (vendor/stb), 不需要 SDL_image/libpng。
 */
#ifndef FXTK_BACKENDS_H
#define FXTK_BACKENDS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 能力协商 ================= */

typedef struct {
    int  has_gpu;          /* 1 = 有 GPU 渲染通道 */
    int  gpu_aa;           /* 0=无 1=SDF 抗锯齿 2=SDF+MSAA */
    int  quad_warp;        /* 0=仅 CPU 逐行透视 1=GPU 透视四边形 */
    int  clipboard;        /* 1 = 系统剪贴板可用 */
    int  file_dialog;      /* 1 = 系统文件对话框可用 */
    int  max_texture;      /* 单张纹理最大边长 (0 = 未知) */
    const char *platform;  /* "linux" / "windows" / "esp32" / "stub" */
} fx_caps_t;

fx_caps_t   fx_backend_caps(void);
const char *fx_backend_name(void);
/* 平台驱动注入屏幕参数 (避免本模块链接 X11/SDL) */
void fx_backend_set_screen(int w, int h, float dpi_scale);
int  fx_screen_w(void);
int  fx_screen_h(void);
float fx_dpi_scale(void);

/* ================= 日志 ================= */

typedef enum { FX_LOG_DEBUG = 0, FX_LOG_INFO, FX_LOG_WARN, FX_LOG_ERROR } fx_log_level_t;
void fx_log_set_level(fx_log_level_t lv);
fx_log_level_t fx_log_get_level(void);
void fx_log(fx_log_level_t lv, const char *fmt, ...);

/* ================= 随机 (PCG32, 可复现) ================= */

void     fx_rand_seed(uint64_t seed);
uint32_t fx_rand_u32(void);
uint32_t fx_rand_below(uint32_t bound);          /* [0, bound) */
int      fx_rand_range(int lo, int hi);          /* [lo, hi] 闭区间 */
float    fx_randf(void);                          /* [0,1) */

/* ================= 时间 ================= */

uint64_t fx_time_us(void);      /* 单调微秒 */
uint64_t fx_time_ms(void);      /* 单调毫秒 */
uint64_t fx_time_unix(void);    /* Unix 秒 (墙钟) */
void     fx_sleep_ms(int ms);

/* ================= 路径 ================= */

/* 拼接 a/b 到 dst (自动补分隔符), 返回写入长度; 失败 <=0 */
int         fx_path_join(char *dst, int cap, const char *a, const char *b);
/* 取文件名部分 (指针指向原串内部) */
const char *fx_path_basename(const char *path);
/* 取扩展名 (含点, 无扩展名返回空串) */
const char *fx_path_ext(const char *path);
/* 取目录部分写入 dst */
int         fx_path_dir(char *dst, int cap, const char *path);
/* 应用数据目录 (不存在则尝试创建), 失败返回 0 */
int         fx_dir_app(char *dst, int cap);

/* ================= 文件 ================= */

int   fx_file_exists(const char *path);
int   fx_file_size(const char *path, long long *out_size);   /* 64 位 (v2.3 修过 Win32 截断) */
/* 整文件读入内存; 成功返回 malloc 的缓冲区(*out_size 为字节数), 需 fx_file_free 释放 */
void *fx_file_read(const char *path, int *out_size);
void  fx_file_free(void *buf);
int   fx_file_write(const char *path, const void *data, int size, int append);
int   fx_dir_create(const char *path);

/* ================= 目录列举 ================= */

typedef struct {
    char name[256];
    int  is_dir;
    long long size;      /* 文件字节数 */
    char date[32];       /* "YYYY-MM-DD HH:MM" */
} fx_dir_entry_t;

/* 列目录 (最多 max 条, 跳过 . 与 ..), 返回条目数, 失败 -1 */
int fx_dir_list(const char *dir, fx_dir_entry_t *out, int max);

/* ================= 图片 (vendored stb) ================= */

typedef struct {
    int w, h, channels;
    unsigned char *pixels;   /* 紧凑排列: channels 字节/像素 */
} fx_img_t;

/* 解码 png/jpg/bmp/gif/tga/... ; 失败返回 0 (out 被清零) */
int fx_img_load_file(const char *path, fx_img_t *out);
int fx_img_load_mem(const void *data, int size, fx_img_t *out);
void fx_img_free(fx_img_t *img);
int fx_img_has_file_ext(const char *path);   /* 后缀是否是可解码图片 */
/* 编码 PNG; 传 RGB(3通道) 或 RGBA(4通道) 紧密缓冲 */
int fx_img_save_png(const char *path, const unsigned char *px, int w, int h, int channels);

/* ================= 剪贴板 ================= */

int fx_clip_set(const char *text);                 /* 成功 1 */
int fx_clip_get(char *buf, int cap);               /* 返回写入长度, 0 = 无内容/不可用 */

/* ================= 偏好 (简单 key-value 持久化) ================= */

int fx_prefs_set(const char *key, const char *val);
int fx_prefs_get(const char *key, char *buf, int cap);   /* 返回长度, 0 = 不存在 */
int fx_prefs_save(void);
int fx_prefs_load(void);

/* ================= 文件对话框 ================= */

/* 选目录 (cap 为 out 容量)。成功 1, 取消/不可用 0 —— 能力见 fx_caps_t.file_dialog */
int fx_backend_pick_dir(char *out, int cap);
/* 选文件; title 可为 NULL, ext_csv 形如 "png,jpg,jpeg" (NULL = 全部) */
int fx_backend_pick_file(char *out, int cap, const char *title, const char *ext_csv);

/* ================= 系统信息 ================= */

int fx_cpu_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FXTK_BACKENDS_H */
