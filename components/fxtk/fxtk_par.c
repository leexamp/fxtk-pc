/**
 * fxtk_par.c — v2.5: CPU 并行 for
 *
 * 设计取舍见 fxtk.h 里的说明。这里只记实现上两个容易踩的点:
 *
 * ① 【小任务必须串行】线程创建约 20~60 µs, 而一个 1000 元素的加法循环只要几 µs。
 *    不设阈值的话, 把小循环丢给 fx_parallel_for 会比直接写 for 慢一个数量级 ——
 *    "加了并行反而变慢", 正是本项目 CHANGELOG 里反复出现的那类陷阱。
 *    所以 n < FXTK_PAR_MIN 一律串行(阈值见下)。
 *
 * ② 【调用线程必须参与干活】不能"主线程只调度"。否则 N 核机器上只有 N-1 核在算,
 *    而且主线程 join 白等。这里让调用线程负责第 0 块, 其余块交给新线程。
 *
 * ③ 【起线程失败要能收敛】pthread_create/CreateThread 可能失败(资源紧张)。
 *    失败时【本线程直接把那一块做完】并把槽位标记为无效, 避免 join 一个没起的线程。
 */
#define _POSIX_C_SOURCE 200809L
#include "fxtk.h"
#include <stdlib.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define FXTK_PAR_WIN32 1
#elif defined(__unix__) || defined(__APPLE__)
  #include <pthread.h>
  #include <unistd.h>
  #define FXTK_PAR_PTHREAD 1
#endif

/* 低于这个规模直接串行: 省下的时间还不如起线程的开销 (见文件头 ①) */
#define FXTK_PAR_MIN 4096
/* 单次调用最多起多少线程: 防止调用方传入 1e6 个元素时按核数也炸出太多线程 */
#define FXTK_PAR_MAX_T 64

static int s_threads = 0;   /* 0 = 自动 */

/* 注意: 这里【不】定义 fx_cpu_count()。它已经在 fxtk_backends.c 里有了(还处理了
 * ESP_PLATFORM / FXTK_BACKEND_STUB 两个特例), 且声明在 fxtk_backends.h。
 * v2.5 初版在这里又实现了一份 → "multiple definition of fx_cpu_count" 链接失败。
 * 本文件要能【独立编译】(par_test 只链它, 不链整个框架), 所以用一份 static 的本地实现,
 * 不引入对外符号。 */
static int par_cpu_count(void)
{
#if defined(FXTK_PAR_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
#elif defined(FXTK_PAR_PTHREAD)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#else
    return 1;
#endif
}

int  fx_parallel_threads(void)      { return s_threads; }
void fx_parallel_set_threads(int n) { s_threads = (n < 0) ? 0 : n; }

static int resolve_threads(int n)
{
    int t = (s_threads > 0) ? s_threads : par_cpu_count();
    if (t > n) t = n;
    if (t > FXTK_PAR_MAX_T) t = FXTK_PAR_MAX_T;
    if (t < 1) t = 1;
    return t;
}

/* ============================ POSIX ============================ */
#if defined(FXTK_PAR_PTHREAD)

typedef struct {
    void (*body)(int lo, int hi, void *ud);
    void *ud;
    int   lo, hi;
    int   alive;         /* 0 = 线程没起来, 已由本线程代做 */
} par_job_t;

static void *par_trampoline(void *p)
{
    par_job_t *j = (par_job_t *)p;
    j->body(j->lo, j->hi, j->ud);
    return NULL;
}

void fx_parallel_for(int n, void (*body)(int lo, int hi, void *ud), void *ud)
{
    if (n <= 0 || !body) return;

    int t = resolve_threads(n);
    if (t <= 1 || n < FXTK_PAR_MIN) { body(0, n, ud); return; }

    pthread_t th[FXTK_PAR_MAX_T];
    par_job_t jobs[FXTK_PAR_MAX_T];
    int chunk = (n + t - 1) / t;

    for (int i = 1; i < t; i++) {
        int lo = i * chunk;
        if (lo >= n) break;
        int hi = lo + chunk;
        if (hi > n) hi = n;
        jobs[i].body = body; jobs[i].ud = ud; jobs[i].lo = lo; jobs[i].hi = hi;
        jobs[i].alive = 0;
        if (pthread_create(&th[i], NULL, par_trampoline, &jobs[i]) == 0) {
            jobs[i].alive = 1;
        } else {
            /* 起不来就本线程顺手做掉, 保证结果完整 (见文件头 ③) */
            body(lo, hi, ud);
        }
    }

    /* 调用线程做第 0 块, 不空等 (见文件头 ②) */
    body(0, chunk < n ? chunk : n, ud);

    for (int i = 1; i < t; i++) if (jobs[i].alive) pthread_join(th[i], NULL);
}

/* ============================ Win32 ============================ */
#elif defined(FXTK_PAR_WIN32)

typedef struct {
    void (*body)(int lo, int hi, void *ud);
    void *ud;
    int   lo, hi;
} par_job_t;

static DWORD WINAPI par_trampoline(LPVOID p)
{
    par_job_t *j = (par_job_t *)p;
    j->body(j->lo, j->hi, j->ud);
    return 0;
}

void fx_parallel_for(int n, void (*body)(int lo, int hi, void *ud), void *ud)
{
    if (n <= 0 || !body) return;

    int t = resolve_threads(n);
    if (t <= 1 || n < FXTK_PAR_MIN) { body(0, n, ud); return; }

    HANDLE   hs[FXTK_PAR_MAX_T];
    par_job_t jobs[FXTK_PAR_MAX_T];
    int chunk = (n + t - 1) / t;
    int started = 1;   /* v2.5.1(L2): 实际创建到的最大索引+1, join 只等到这里 */
    memset(hs, 0, sizeof(hs));

    for (int i = 1; i < t; i++) {
        int lo = i * chunk;
        if (lo >= n) break;
        int hi = lo + chunk;
        if (hi > n) hi = n;
        jobs[i].body = body; jobs[i].ud = ud; jobs[i].lo = lo; jobs[i].hi = hi;
        hs[i] = CreateThread(NULL, 0, par_trampoline, &jobs[i], 0, NULL);
        if (!hs[i]) body(lo, hi, ud);   /* 失败就本线程代做 */
        else started = i + 1;
    }

    body(0, chunk < n ? chunk : n, ud);

    for (int i = 1; i < started; i++) {   /* v2.5.1(L2): 未创建的 hs[] 是栈垃圾, 不能等 */
        if (hs[i]) { WaitForSingleObject(hs[i], INFINITE); CloseHandle(hs[i]); }
    }
}

/* ============================ 串行退化 (ESP32 等) ============================ */
#else

void fx_parallel_for(int n, void (*body)(int lo, int hi, void *ud), void *ud)
{
    if (n <= 0 || !body) return;
    body(0, n, ud);   /* 无线程库: 语义不变, 只是不并行 */
}

#endif
