/**
 * fxtk_backends.c — v2.4 通用跨平台服务层实现
 *
 * 编译期选择实现: -DFXTK_BACKEND_STUB / _WIN32 / ESP_PLATFORM / 其它(POSIX)
 * 详见 fxtk_backends.h 的设计原则。
 */
#define _POSIX_C_SOURCE 200809L
#include "fxtk_backends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>
  #include <io.h>
  #include <wchar.h>   /* wcsncat/wcslen (对话框过滤器) */
  #include <shlobj.h>  /* SHBrowseForFolderW / CoTaskMemFree */
  #include <commdlg.h> /* GetOpenFileNameW (需 -lcomdlg32) */
#elif defined(ESP_PLATFORM)
  #include <dirent.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include "esp_log.h"
  #include "esp_timer.h"
  /* 本文件 170 行附近用 vTaskDelay(pdMS_TO_TICKS(...)) 做延时 —— 以前没包含 FreeRTOS 头,
   * 靠别处传递包含侥幸编过(ESP32 冒烟用 -Werror=implicit-function-declaration 抓出来的)。 */
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#else
  #include <unistd.h>
  #include <dirent.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

/* ---- 图片编解码: vendored stb, 只开需要的解码器 (体积) ---- */
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb/stb_image_write.h"

/* ================= 能力与屏幕参数 ================= */

static int   s_scr_w = 480, s_scr_h = 272;
static float s_dpi = 1.0f;

const char *fx_backend_name(void)
{
#if defined(FXTK_BACKEND_STUB)
    return "stub";
#elif defined(_WIN32)
    return "windows";
#elif defined(ESP_PLATFORM)
    return "esp32";
#else
    return "linux";
#endif
}

fx_caps_t fx_backend_caps(void)
{
    fx_caps_t c;
    memset(&c, 0, sizeof(c));
    c.platform = fx_backend_name();
    c.max_texture = 4096;
#if defined(FXTK_BACKEND_STUB)
    c.has_gpu = 0; c.gpu_aa = 0; c.quad_warp = 0; c.clipboard = 0; c.file_dialog = 0;
#elif defined(ESP_PLATFORM)
    c.has_gpu = 0; c.gpu_aa = 0; c.quad_warp = 0; c.clipboard = 0; c.file_dialog = 0;
#elif defined(_WIN32)
    c.has_gpu = 1; c.gpu_aa = 2; c.quad_warp = 1; c.clipboard = 1; c.file_dialog = 1;
#else
    c.has_gpu = 1; c.gpu_aa = 2; c.quad_warp = 1; c.clipboard = 1; c.file_dialog = 1;
#endif
    return c;
}

void  fx_backend_set_screen(int w, int h, float dpi_scale)
{
    if (w > 0) s_scr_w = w;
    if (h > 0) s_scr_h = h;
    if (dpi_scale > 0.1f) s_dpi = dpi_scale;
}
int   fx_screen_w(void) { return s_scr_w; }
int   fx_screen_h(void) { return s_scr_h; }
float fx_dpi_scale(void) { return s_dpi; }

/* ================= 日志 ================= */

static fx_log_level_t s_log_level = FX_LOG_INFO;
void fx_log_set_level(fx_log_level_t lv) { s_log_level = lv; }
fx_log_level_t fx_log_get_level(void) { return s_log_level; }

void fx_log(fx_log_level_t lv, const char *fmt, ...)
{
    if (lv < s_log_level) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#if defined(ESP_PLATFORM)
    static const char *tags[] = { "fxtk-d", "fxtk-i", "fxtk-w", "fxtk-e" };
    esp_log_write((esp_log_level_t)lv, tags[lv & 3], "%s\n", buf);
#else
    static const char *pfx[] = { "[D]", "[I]", "[W]", "[E]" };
    fprintf(stderr, "%s %s\n", pfx[lv & 3], buf);
#endif
}

/* ================= 随机 (PCG32) ================= */

static uint64_t s_rng_state = 0x853c49e6748fea9bULL;
static uint64_t s_rng_inc   = 0xda3e39cb94b95bdbULL;

uint32_t fx_rand_u32(void)
{
    uint64_t old = s_rng_state;
    s_rng_state = old * 6364136223846793005ULL + s_rng_inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}
void fx_rand_seed(uint64_t seed)
{
    s_rng_state = 0;
    s_rng_inc = (seed << 1u) | 1u;
    (void)fx_rand_u32();
    s_rng_state += seed;
    (void)fx_rand_u32();
}
uint32_t fx_rand_below(uint32_t bound)
{
    if (bound == 0) return 0;
    uint32_t threshold = (uint32_t)(-bound) % bound;   /* 去偏 */
    for (;;) {
        uint32_t r = fx_rand_u32();
        if (r >= threshold) return r % bound;
    }
}
int   fx_rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)fx_rand_below((uint32_t)(hi - lo + 1));
}
float fx_randf(void) { return (float)(fx_rand_u32() >> 8) * (1.0f / 16777216.0f); }

/* ================= 时间 ================= */

#if defined(FXTK_BACKEND_STUB)
static uint64_t s_stub_us = 1000000;     /* 起始 1s, 只在 sleep 时推进 → 测试确定性 */
uint64_t fx_time_us(void)   { return s_stub_us; }
uint64_t fx_time_ms(void)   { return s_stub_us / 1000; }
uint64_t fx_time_unix(void) { return 1700000000ULL + s_stub_us / 1000000ULL; }
void     fx_sleep_ms(int ms) { if (ms > 0) s_stub_us += (uint64_t)ms * 1000; }
#elif defined(ESP_PLATFORM)
uint64_t fx_time_us(void)   { return (uint64_t)esp_timer_get_time(); }
uint64_t fx_time_ms(void)   { return (uint64_t)esp_timer_get_time() / 1000; }
uint64_t fx_time_unix(void) { return (uint64_t)time(NULL); }
void     fx_sleep_ms(int ms) { if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms)); }
#elif defined(_WIN32)
uint64_t fx_time_us(void)
{
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER now;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1000000.0 / (double)freq.QuadPart);
}
uint64_t fx_time_ms(void)   { return fx_time_us() / 1000; }
uint64_t fx_time_unix(void) { return (uint64_t)time(NULL); }
void     fx_sleep_ms(int ms) { if (ms > 0) Sleep((DWORD)ms); }
#else
uint64_t fx_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}
uint64_t fx_time_ms(void)   { return fx_time_us() / 1000; }
uint64_t fx_time_unix(void) { return (uint64_t)time(NULL); }
void     fx_sleep_ms(int ms)
{
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* ================= 路径 ================= */

static int is_sep(char c) { return c == '/' || c == '\\'; }

int fx_path_join(char *dst, int cap, const char *a, const char *b)
{
    if (!dst || cap <= 0) return -1;
    dst[0] = 0;
    if (!a) a = "";
    if (!b) b = "";
    int al = (int)strlen(a);
    int need_sep = al > 0 && !is_sep(a[al - 1]) && b[0] && !is_sep(b[0]);
    int n = snprintf(dst, (size_t)cap, "%s%s%s", a, need_sep ? "/" : "", b);
    if (n < 0) { dst[0] = 0; return -1; }
    if (n >= cap) { dst[cap - 1] = 0; return cap - 1; }
    return n;
}

const char *fx_path_basename(const char *path)
{
    if (!path) return "";
    const char *last = path;
    for (const char *p = path; *p; p++) if (is_sep(*p)) last = p + 1;
    return last;
}

const char *fx_path_ext(const char *path)
{
    const char *base = fx_path_basename(path);
    const char *dot = strrchr(base, '.');
    return dot ? dot : "";
}

int fx_path_dir(char *dst, int cap, const char *path)
{
    if (!dst || cap <= 0) return -1;
    dst[0] = 0;
    if (!path) return -1;
    const char *base = fx_path_basename(path);
    int n = (int)(base - path);
    if (n <= 0) { n = 0; }                      /* 无分隔符 → 当前目录 */
    while (n > 1 && is_sep(path[n - 1])) n--;   /* 去掉尾部分隔符 */
    if (n >= cap) n = cap - 1;
    memcpy(dst, path, (size_t)n);
    dst[n] = 0;
    return n;
}

int fx_dir_app(char *dst, int cap)
{
    if (!dst || cap <= 0) return 0;
    const char *env = getenv("FXTK_APP_DIR");
    if (env && env[0]) { snprintf(dst, (size_t)cap, "%s", env); }
    else {
#if defined(_WIN32)
        const char *appdata = getenv("APPDATA");
        snprintf(dst, (size_t)cap, "%s%sfxtk", appdata ? appdata : ".", appdata ? "\\" : "", "");
#elif defined(FXTK_BACKEND_STUB)
        snprintf(dst, (size_t)cap, "/tmp/fxtk-stub-app");
#else
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        if (xdg && xdg[0]) snprintf(dst, (size_t)cap, "%s/fxtk", xdg);
        else if (home && home[0]) snprintf(dst, (size_t)cap, "%s/.config/fxtk", home);
        else snprintf(dst, (size_t)cap, "/tmp/fxtk-app");
#endif
    }
    (void)fx_dir_create(dst);   /* 尽力创建, 失败不致命 */
    return 1;
}

/* ================= 文件 ================= */

#if defined(FXTK_BACKEND_STUB)

/* --- stub: 内存文件系统 (测试确定性, 不碰真实磁盘) --- */
#define STUB_FS_MAX 32
static struct { char path[256]; unsigned char *data; int size; } s_stub_fs[STUB_FS_MAX];
static int s_stub_fs_n = 0;

static int stub_find(const char *path) {
    for (int i = 0; i < s_stub_fs_n; i++) if (strcmp(s_stub_fs[i].path, path) == 0) return i;
    return -1;
}
int fx_file_exists(const char *path) { return path && stub_find(path) >= 0; }
int fx_file_size(const char *path, long long *out)
{
    int i = stub_find(path);
    if (i < 0) return 0;
    if (out) *out = (long long)s_stub_fs[i].size;
    return 1;
}
void *fx_file_read(const char *path, int *out_size)
{
    int i = stub_find(path);
    if (out_size) *out_size = 0;
    if (i < 0) return NULL;
    void *buf = malloc((size_t)(s_stub_fs[i].size ? s_stub_fs[i].size : 1));
    if (!buf) return NULL;
    memcpy(buf, s_stub_fs[i].data, (size_t)s_stub_fs[i].size);
    if (out_size) *out_size = s_stub_fs[i].size;
    return buf;
}
int fx_file_write(const char *path, const void *data, int size, int append)
{
    if (!path || size < 0) return 0;
    int i = stub_find(path);
    if (i >= 0 && !append) { free(s_stub_fs[i].data); s_stub_fs[i].data = NULL; s_stub_fs[i].size = 0; }
    if (size == 0) return 1;                     /* 清空/建空文件: realloc(p,0) 会返回 NULL, 需特判 */
    if (i < 0) {
        if (s_stub_fs_n >= STUB_FS_MAX) return 0;
        i = s_stub_fs_n++;
        snprintf(s_stub_fs[i].path, sizeof(s_stub_fs[i].path), "%s", path);
        s_stub_fs[i].data = NULL; s_stub_fs[i].size = 0;
    }
    int old = s_stub_fs[i].size;
    unsigned char *nb = (unsigned char *)realloc(s_stub_fs[i].data, (size_t)(old + size));
    if (!nb) return 0;
    memcpy(nb + old, data, (size_t)size);
    s_stub_fs[i].data = nb;
    s_stub_fs[i].size = old + size;
    return 1;
}
int fx_dir_create(const char *path) { (void)path; return 1; }

#else   /* 真实文件系统 */

int fx_file_exists(const char *path)
{
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
#if defined(_WIN32)
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

int fx_file_size(const char *path, long long *out)
{
    if (!path) return 0;
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA fd;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fd)) return 0;
    if (out) *out = (long long)(((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
    return 1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (out) *out = (long long)st.st_size;
    return 1;
#endif
}

void *fx_file_read(const char *path, int *out_size)
{
    if (out_size) *out_size = 0;
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long sz = 0;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = sz > 0 ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    ((char *)buf)[got] = 0;
    if (out_size) *out_size = (int)got;
    return buf;
}

int fx_file_write(const char *path, const void *data, int size, int append)
{
    if (!path || size < 0 || (!data && size > 0)) return 0;
    FILE *f = fopen(path, append ? "ab" : "wb");
    if (!f) return 0;
    size_t wrote = size > 0 ? fwrite(data, 1, (size_t)size, f) : 0;
    fclose(f);
    return wrote == (size_t)size;
}

int fx_dir_create(const char *path)
{
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    return _mkdir(path) == 0 || fx_file_exists(path);
#else
    return mkdir(path, 0755) == 0 || fx_file_exists(path);
#endif
}

#endif  /* FXTK_BACKEND_STUB */

/* 公共: 释放 fx_file_read 返回的缓冲区 (两个分支共用) */
void fx_file_free(void *buf) { free(buf); }

/* ================= 目录列举 ================= */

#if defined(FXTK_BACKEND_STUB)

int fx_dir_list(const char *dir, fx_dir_entry_t *out, int max)
{
    if (!dir || !out || max <= 0) return -1;
    int n = 0;
    size_t dl = strlen(dir);
    for (int i = 0; i < s_stub_fs_n && n < max; i++) {
        if (strncmp(s_stub_fs[i].path, dir, dl) != 0) continue;
        const char *base = fx_path_basename(s_stub_fs[i].path);
        snprintf(out[n].name, sizeof(out[n].name), "%.*s", (int)sizeof(out[n].name) - 1, base);
        out[n].is_dir = 0;
        out[n].size = s_stub_fs[i].size;
        snprintf(out[n].date, sizeof(out[n].date), "2000-01-01 00:00");
        n++;
    }
    return n;
}

#elif defined(_WIN32)

int fx_dir_list(const char *dir, fx_dir_entry_t *out, int max)
{
    if (!dir || !out || max <= 0) return -1;
    wchar_t pattern[1024];
    int dn = MultiByteToWideChar(CP_UTF8, 0, dir, -1, pattern, 1024);
    if (dn <= 0 || dn > 1020) return -1;
    pattern[dn - 1] = L'\\'; pattern[dn] = L'*'; pattern[dn + 1] = 0;   /* v2.3: 显式限长拼接 */
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, out[n].name, sizeof(out[n].name), NULL, NULL)) continue;
        out[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        out[n].size = (long long)(((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        SYSTEMTIME st; FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
        snprintf(out[n].date, sizeof(out[n].date), "%04d-%02d-%02d %02d:%02d",
                 (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
        n++;
    } while (n < max && FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

#else

int fx_dir_list(const char *dir, fx_dir_entry_t *out, int max)
{
    if (!dir || !out || max <= 0) return -1;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
        snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        out[n].size = (long long)st.st_size;
        struct tm tmv; localtime_r(&st.st_mtime, &tmv);
        strftime(out[n].date, sizeof(out[n].date), "%Y-%m-%d %H:%M", &tmv);
        n++;
    }
    closedir(d);
    return n;
}

#endif

/* ================= 图片 ================= */

int fx_img_load_mem(const void *data, int size, fx_img_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data || size <= 0) return 0;
    int w = 0, h = 0, ch = 0;
    unsigned char *px = stbi_load_from_memory((const stbi_uc *)data, size, &w, &h, &ch, 0);
    if (!px) { fx_log(FX_LOG_WARN, "图片解码失败: %s", stbi_failure_reason()); return 0; }
    out->w = w; out->h = h; out->channels = ch; out->pixels = px;
    return 1;
}

int fx_img_load_file(const char *path, fx_img_t *out)
{
    int size = 0;
    void *buf = fx_file_read(path, &size);
    if (!buf) { if (out) memset(out, 0, sizeof(*out)); return 0; }
    int ok = fx_img_load_mem(buf, size, out);
    fx_file_free(buf);
    return ok;
}

void fx_img_free(fx_img_t *img)
{
    if (!img) return;
    if (img->pixels) stbi_image_free(img->pixels);
    memset(img, 0, sizeof(*img));
}

int fx_img_has_file_ext(const char *path)
{
    const char *ext = fx_path_ext(path);
    static const char *ok[] = { ".png", ".PNG", ".jpg", ".JPG", ".jpeg", ".JPEG",
                                ".bmp", ".BMP", ".gif", ".GIF", ".tga", ".TGA" };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        if (strcmp(ext, ok[i]) == 0) return 1;
    return 0;
}

/* 编码回调: 统一走 fx_file_write, 这样 stub(内存 FS)与真实 FS 行为一致 */
typedef struct { const char *path; int failed; } png_sink_t;
static void png_sink_write(void *ctx, void *data, int size)
{
    png_sink_t *s = (png_sink_t *)ctx;
    if (s->failed || size <= 0) return;
    if (!fx_file_write(s->path, data, size, 1)) s->failed = 1;
}

int fx_img_save_png(const char *path, const unsigned char *px, int w, int h, int channels)
{
    if (!path || !px || w <= 0 || h <= 0 || (channels != 3 && channels != 4)) return 0;
    if (!fx_file_write(path, "", 0, 0)) return 0;      /* 清空/创建目标 */
    png_sink_t sink; sink.path = path; sink.failed = 0;
    int ok = stbi_write_png_to_func(png_sink_write, &sink, w, h, channels, px, w * channels);
    return ok && !sink.failed;
}

/* ================= 剪贴板 ================= */

#if defined(FXTK_BACKEND_STUB)

static char s_stub_clip[2048];
int fx_clip_set(const char *text) { snprintf(s_stub_clip, sizeof(s_stub_clip), "%s", text ? text : ""); return 1; }
int fx_clip_get(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    int n = (int)strlen(s_stub_clip);
    if (n >= cap) n = cap - 1;
    memcpy(buf, s_stub_clip, (size_t)n); buf[n] = 0;
    return n;
}

#elif defined(_WIN32)

int fx_clip_set(const char *text)
{
    if (!text) return 0;
    int wn = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wn <= 0) return 0;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wn * sizeof(wchar_t));
    if (!mem) return 0;
    wchar_t *dst = (wchar_t *)GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, dst, wn);
    GlobalUnlock(mem);
    if (!OpenClipboard(NULL)) { GlobalFree(mem); return 0; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) { CloseClipboard(); GlobalFree(mem); return 0; }
    CloseClipboard();
    return 1;
}

int fx_clip_get(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    buf[0] = 0;
    if (!OpenClipboard(NULL)) return 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    int n = 0;
    if (h) {
        const wchar_t *src = (const wchar_t *)GlobalLock(h);
        if (src) {
            n = WideCharToMultiByte(CP_UTF8, 0, src, -1, buf, cap, NULL, NULL);
            GlobalUnlock(h);
            if (n > 0) n--;               /* 去掉结尾 NUL */
        }
    }
    CloseClipboard();
    return n > 0 ? n : 0;
}

#elif defined(ESP_PLATFORM)

int fx_clip_set(const char *text) { (void)text; return 0; }   /* ESP32 无系统剪贴板 */
int fx_clip_get(char *buf, int cap) { if (buf && cap > 0) buf[0] = 0; return 0; }

#else

/* Linux: 无 sokol/SDL 依赖的剪贴板 —— 借外部命令 (Wayland 优先, 再 X11)。
 * 这在能力查询里如实报告: 两个命令都没有时返回失败, 框架退化为内部剪贴板。 */
static int have_cmd(const char *cmd)
{
    char line[128];
    snprintf(line, sizeof(line), "command -v %s >/dev/null 2>&1", cmd);
    return system(line) == 0;
}

int fx_clip_set(const char *text)
{
    if (!text) return 0;
    const char *cmd = have_cmd("wl-copy") ? "wl-copy" : (have_cmd("xclip") ? "xclip -selection clipboard" : NULL);
    if (!cmd) return 0;
    char full[128];
    snprintf(full, sizeof(full), "%s 2>/dev/null", cmd);
    FILE *p = popen(full, "w");
    if (!p) return 0;
    fwrite(text, 1, strlen(text), p);
    return pclose(p) == 0;
}

int fx_clip_get(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    buf[0] = 0;
    const char *cmd = have_cmd("wl-paste") ? "wl-paste --no-newline"
                    : (have_cmd("xclip") ? "xclip -o -selection clipboard" : NULL);
    if (!cmd) return 0;
    char full[160];
    snprintf(full, sizeof(full), "%s 2>/dev/null", cmd);
    FILE *p = popen(full, "r");
    if (!p) return 0;
    size_t n = fread(buf, 1, (size_t)cap - 1, p);
    buf[n] = 0;
    pclose(p);
    return (int)n;
}

#endif

/* ================= 偏好 (key-value 持久化) ================= */

#define PREFS_MAX 64
static struct { char k[32]; char v[160]; } s_prefs[PREFS_MAX];
static int s_prefs_n = 0;

static int prefs_find(const char *key)
{
    for (int i = 0; i < s_prefs_n; i++) if (strcmp(s_prefs[i].k, key) == 0) return i;
    return -1;
}

int fx_prefs_set(const char *key, const char *val)
{
    if (!key || !key[0]) return 0;
    int i = prefs_find(key);
    if (i < 0) {
        if (s_prefs_n >= PREFS_MAX) return 0;
        i = s_prefs_n++;
        snprintf(s_prefs[i].k, sizeof(s_prefs[i].k), "%s", key);
    }
    snprintf(s_prefs[i].v, sizeof(s_prefs[i].v), "%s", val ? val : "");
    return 1;
}

int fx_prefs_get(const char *key, char *buf, int cap)
{
    if (!key || !buf || cap <= 0) return 0;
    buf[0] = 0;
    int i = prefs_find(key);
    if (i < 0) return 0;
    int n = (int)strlen(s_prefs[i].v);
    if (n >= cap) n = cap - 1;
    memcpy(buf, s_prefs[i].v, (size_t)n);
    buf[n] = 0;
    return n;
}

int fx_prefs_save(void)
{
    char dir[512], path[600];
    if (!fx_dir_app(dir, sizeof(dir))) return 0;
    if (!fx_path_join(path, sizeof(path), dir, "fxtk_prefs.ini")) return 0;
    char blob[PREFS_MAX * 200];
    int off = 0;
    for (int i = 0; i < s_prefs_n; i++)
        off += snprintf(blob + off, sizeof(blob) - (size_t)off, "%s=%s\n", s_prefs[i].k, s_prefs[i].v);
    return fx_file_write(path, blob, off, 0);
}

int fx_prefs_load(void)
{
    char dir[512], path[600];
    if (!fx_dir_app(dir, sizeof(dir))) return 0;
    if (!fx_path_join(path, sizeof(path), dir, "fxtk_prefs.ini")) return 0;
    int size = 0;
    char *blob = (char *)fx_file_read(path, &size);
    if (!blob) return 0;
    s_prefs_n = 0;
    char *line = blob;
    while (line && *line && s_prefs_n < PREFS_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *eq = strchr(line, '=');
        if (eq) { *eq = 0; fx_prefs_set(line, eq + 1); }
        line = nl ? nl + 1 : NULL;
    }
    fx_file_free(blob);
    return 1;
}

/* ================= 文件对话框 ================= */

#if defined(FXTK_BACKEND_STUB)

int fx_backend_pick_dir(char *out, int cap) { if (out && cap > 0) out[0] = 0; return 0; }
int fx_backend_pick_file(char *out, int cap, const char *title, const char *ext_csv)
{ (void)title; (void)ext_csv; if (out && cap > 0) out[0] = 0; return 0; }

#elif defined(ESP_PLATFORM)

int fx_backend_pick_dir(char *out, int cap) { if (out && cap > 0) out[0] = 0; return 0; }
int fx_backend_pick_file(char *out, int cap, const char *title, const char *ext_csv)
{ (void)title; (void)ext_csv; if (out && cap > 0) out[0] = 0; return 0; }

#elif defined(_WIN32)

int fx_backend_pick_dir(char *out, int cap)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BROWSEINFOW bi; memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = L"选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) { CoUninitialize(); return 0; }
    wchar_t path[MAX_PATH];
    int ok = SHGetPathFromIDListW(pidl, path);
    int wn = ok ? WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL) : 0;
    CoTaskMemFree(pidl);
    CoUninitialize();
    return (wn > 0 && out[0]) ? 1 : 0;
}

int fx_backend_pick_file(char *out, int cap, const char *title, const char *ext_csv)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    /* 过滤器: "png,jpg" → "*.png;*.jpg" (UTF-16) */
    wchar_t filter[512] = L"所有文件\0*.*\0";
    if (ext_csv && ext_csv[0]) {
        wchar_t pats[256] = L"";
        char csv[256];
        snprintf(csv, sizeof(csv), "%s", ext_csv);
        for (char *tok = strtok(csv, ",; "); tok; tok = strtok(NULL, ",; ")) {
            wchar_t wpat[64];
            char one[80];
            snprintf(one, sizeof(one), "%s*.%s", pats[0] ? ";" : "", tok);
            MultiByteToWideChar(CP_UTF8, 0, one, -1, wpat, 64);
            wcsncat(pats, wpat, 200);
        }
        wchar_t wdesc[128]; MultiByteToWideChar(CP_UTF8, 0, "图片", -1, wdesc, 128);
        size_t dl = wcslen(wdesc) + 1, pl = wcslen(pats) + 1;
        memcpy(filter, wdesc, dl * sizeof(wchar_t));
        memcpy(filter + dl, pats, pl * sizeof(wchar_t));
        filter[dl + pl] = 0;
    }
    wchar_t wtitle[128] = L"选择文件";
    if (title) MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 128);

    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    wchar_t file[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, out, cap, NULL, NULL);
    return (n > 0 && out[0]) ? 1 : 0;
}

#else   /* Linux/其它: zenity / kdialog */

static int have_cmd(const char *cmd);
static int run_dialog(const char *cmd, char *out, int cap)
{
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    char *r = fgets(out, cap, p);
    int rc = pclose(p);
    if (!r || rc != 0) { out[0] = 0; return 0; }
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    return out[0] ? 1 : 0;
}

/* 把 "png,jpg,..." 变成 "*.png *.jpg ..."(单一过滤器串 —— 旧实现往 96 字节小缓冲里反复
 * 拼接多个 --file-filter, 6 个扩展名必然截断, zenity 收到畸形参数直接失败: 这就是"Linux 无法导入图片")。 */
static void ext_patterns(const char *ext_csv, char *out, int cap)
{
    out[0] = 0;
    if (!ext_csv || !ext_csv[0] || cap < 8) return;
    char csv[160];
    snprintf(csv, sizeof(csv), "%s", ext_csv);
    int n = 0;
    for (char *tok = strtok(csv, ",; "); tok; tok = strtok(NULL, ",; ")) {
        int w = snprintf(out + n, (size_t)(cap - n), "%s*.%s", n ? " " : "", tok);
        if (w <= 0 || w >= cap - n) break;
        n += w;
    }
}

/* 标题里若带单引号会截断 shell 命令, 直接剔除(标点而已, 不值得为它引号转义) */
static void safe_title(const char *title, char *out, int cap)
{
    const char *t = (title && title[0]) ? title : "选择文件";
    int i = 0;
    for (; t[i] && i < cap - 1; i++) out[i] = (t[i] == '\'' || t[i] == '"' || t[i] == '\n') ? ' ' : t[i];
    out[i] = 0;
}

int fx_backend_pick_dir(char *out, int cap)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (have_cmd("zenity") &&
        run_dialog("zenity --file-selection --directory --title='选择文件夹' 2>/dev/null", out, cap)) return 1;
    if (have_cmd("kdialog") &&
        run_dialog("kdialog --getexistingdirectory . 2>/dev/null", out, cap)) return 1;
    return 0;
}

int fx_backend_pick_file(char *out, int cap, const char *title, const char *ext_csv)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    char pats[256], ttl[128], cmd[768];
    ext_patterns(ext_csv, pats, sizeof(pats));
    safe_title(title, ttl, sizeof(ttl));
    if (getenv("FXTK_PICK_DEBUG")) fprintf(stderr, "[pick] zenity=%d kdialog=%d pats='%s'\n",
                                           have_cmd("zenity"), have_cmd("kdialog"), pats);
    if (have_cmd("zenity")) {                       /* 失败必须继续尝试 kdialog: 只试一个就放弃是旧实现的第二个坑 */
        if (pats[0]) snprintf(cmd, sizeof(cmd), "zenity --file-selection --title='%s' --file-filter='图片 | %s' 2>/dev/null", ttl, pats);
        else         snprintf(cmd, sizeof(cmd), "zenity --file-selection --title='%s' 2>/dev/null", ttl);
        if (getenv("FXTK_PICK_DEBUG")) fprintf(stderr, "[pick] %s\n", cmd);
        if (run_dialog(cmd, out, cap)) return 1;
    }
    if (have_cmd("kdialog")) {
        if (pats[0]) snprintf(cmd, sizeof(cmd), "kdialog --getopenfilename . '%s' --title '%s' 2>/dev/null", pats, ttl);
        else         snprintf(cmd, sizeof(cmd), "kdialog --getopenfilename . 2>/dev/null");
        if (getenv("FXTK_PICK_DEBUG")) fprintf(stderr, "[pick] %s\n", cmd);
        if (run_dialog(cmd, out, cap)) return 1;
    }
    return 0;
}

#endif

/* ================= 系统信息 ================= */

int fx_cpu_count(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
#elif defined(ESP_PLATFORM)
    int n = 1;
#elif defined(FXTK_BACKEND_STUB)
    int n = 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    int ni = (int)n;
    return ni > 0 ? ni : 1;
#endif
    return n > 0 ? n : 1;
}
