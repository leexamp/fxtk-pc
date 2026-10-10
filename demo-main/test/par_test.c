/**
 * par_test.c — v2.5 并行 for 自检 (无头, 不需要窗口/显示)
 *
 * 重点不在"能不能跑", 而在【每个元素恰好被处理一次】。
 * 切块逻辑的典型 bug 是边界算错 —— 要么漏掉尾部一组, 要么两组都写同一段
 * (多线程下这是静默的数据竞争: 不崩、不报错、结果偶发不对)。
 * 所以这里用一个 count[] 记录每个元素被碰过几次, 全部必须【正好 1 次】。
 */
#include "fxtk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
  #include <pthread.h>
  /* 记录 body 跑在哪些线程上 —— 用来【确定性地】证明并行真的发生了 */
  static pthread_t       s_tids[64];
  static int             s_tid_n = 0;
  static pthread_mutex_t s_tid_lk = PTHREAD_MUTEX_INITIALIZER;

  static void fx_tid_reset(pthread_t *out, int *n_out) { (void)out; *n_out = 0; s_tid_n = 0; }

  static void body_tid(int lo, int hi, void *ud)
  {
      (void)lo; (void)hi; (void)ud;
      pthread_t me = pthread_self();
      pthread_mutex_lock(&s_tid_lk);
      int seen = 0;
      for (int i = 0; i < s_tid_n; i++) if (pthread_equal(s_tids[i], me)) { seen = 1; break; }
      if (!seen && s_tid_n < 64) s_tids[s_tid_n++] = me;
      pthread_mutex_unlock(&s_tid_lk);
  }
#endif

static int s_fail = 0;

static void chk(const char *name, int ok)
{
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) s_fail++;
}

/* 每个元素 +1, 同时把 value 写成 i*i (验证"值"和"次数"两件事) */
static void body_sq(int lo, int hi, void *ud)
{
    long *cnt = (long *)ud;
    for (int i = lo; i < hi; i++) cnt[i] += 1;
}

/* 用一个二维数组: [0..n-1]=次数, [n..2n-1]=值 */
static void body_val(int lo, int hi, void *ud)
{
    long *buf = (long *)ud;
    for (int i = lo; i < hi; i++) { buf[i] += 1; buf[i + 100000] = (long)i * i; }
}

static void run_case(int n)
{
    long *cnt = (long *)calloc((size_t)n + 1, sizeof(long));
    if (!cnt) { chk("calloc", 0); return; }

    fx_parallel_for(n, body_sq, cnt);

    long bad = -1, badcnt = 0;
    for (int i = 0; i < n; i++) if (cnt[i] != 1) { if (bad < 0) bad = i; badcnt++; }
    char nm[96];
    snprintf(nm, sizeof nm, "n=%d: 每个元素恰好被处理 1 次 (count 全 = 1)", n);
    chk(nm, badcnt == 0);
    if (badcnt) printf("         首个异常下标=%ld, 异常个数=%ld\n", bad, badcnt);

    free(cnt);
}

int main(void)
{
    printf("== fxtk v2.5 并行 for 自检 ==\n");
    printf("  默认线程设定: %d (0 = 自动, 按 CPU 核数)\n", fx_parallel_threads());

    chk("fx_parallel_for(NULL body) 不崩", (fx_parallel_for(100, NULL, NULL), 1));
    chk("fx_parallel_for(n=0) 不崩", (fx_parallel_for(0, body_sq, NULL), 1));
    chk("fx_parallel_for(n<0) 不崩", (fx_parallel_for(-5, body_sq, NULL), 1));

    printf("\n-- 语法糖 fx_par_for --\n");
    {
        int a[8];
        fx_par_for(8) { a[fx_i] = fx_i * fx_i; }
        int ok = 1;
        for (int i = 0; i < 8; i++) if (a[i] != i * i) ok = 0;
        chk("fx_par_for(8) 产出 0,1,4,9,...,49", ok);
        printf("         ");
        for (int i = 0; i < 8; i++) printf("%d ", a[i]);
        printf("\n");
    }

    printf("\n-- 边界规模 (切块易错处) --\n");
    int sizes[] = { 1, 2, 3, 7, 100, 4095, 4096, 4097, 10000, 65537 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) run_case(sizes[i]);

    printf("\n-- 强制串行 / 强制多线程 结果一致 --\n");
    {
        int n = 20000;
        long *c1 = (long *)calloc((size_t)n, sizeof(long));
        long *c2 = (long *)calloc((size_t)n, sizeof(long));
        fx_parallel_set_threads(1);
        fx_parallel_for(n, body_sq, c1);
        fx_parallel_set_threads(4);
        fx_parallel_for(n, body_sq, c2);
        fx_parallel_set_threads(0);
        chk("串行(1 线程)与 4 线程结果逐元素相同",
            memcmp(c1, c2, (size_t)n * sizeof(long)) == 0);
        free(c1); free(c2);
    }

    printf("\n-- 值正确性 (i*i) 且无写串 --\n");
    {
        int n = 30000;
        long *buf = (long *)calloc(200000, sizeof(long));
        fx_parallel_for(n, body_val, buf);
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (buf[i] != 1) { ok = 0; break; }
            if (buf[i + 100000] != (long)i * i) { ok = 0; break; }
        }
        chk("30000 个元素的值 = i*i 且各写 1 次", ok);
        free(buf);
    }

#if defined(__unix__) || defined(__APPLE__)
    printf("\n-- 真的用了多线程吗? (数不同线程 id, 不看耗时) --\n");
    {
        /* 为什么不用"计时"来判断并行是否生效: 耗时受负载/调频影响极大,
         * 本项目 CHANGELOG 里那条"两轮 A/B 结论相反"就是被这个坑过。
         * 线程 id 是确定性的: 数到 >=2 个不同 id, 就证明 body 真的在不同线程上跑过。 */
        pthread_t tids[64];
        int n_tids = 0;
        (void)tids;
        fx_tid_reset(tids, &n_tids);
        fx_parallel_set_threads(4);
        fx_parallel_for(2000000, body_tid, NULL);
        fx_parallel_set_threads(0);
        n_tids = s_tid_n;   /* body_tid 累加的是 s_tid_n, 不是局部变量 */
        chk("body 在 >=2 个不同线程上执行过 (确定性证据)", n_tids >= 2);
        printf("         实测线程数 = %d (请求 4)\n", n_tids);
    }
#endif

    printf("\n== 结果: %s (失败 %d 项) ==\n", s_fail ? "FAIL" : "PASS", s_fail);
    return s_fail ? 1 : 0;
}
