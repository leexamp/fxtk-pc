#ifdef _WIN32
#include <windows.h>
#endif
/**
 * raymarch.c — v5
 * 【v5】pthread 行级并行: N 核同时光追, 帧率 ≈ 单核 × N
 * 场景同 v4: 解析地面/球 + 步进圆环 + 软阴影 + 雾
 */
#include "raymarch.h"
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>   /* v2.3.1: worker 启动自旋用 sched_yield */

typedef struct { float x, y, z; } v3;
static v3 V(float x, float y, float z) { v3 r = {x,y,z}; return r; }
static v3 vadd(v3 a, v3 b) { return V(a.x+b.x, a.y+b.y, a.z+b.z); }
static v3 vsub(v3 a, v3 b) { return V(a.x-b.x, a.y-b.y, a.z-b.z); }
static v3 vmul(v3 a, float t) { return V(a.x*t, a.y*t, a.z*t); }
static float vdot(v3 a, v3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static v3 vcross(v3 a, v3 b) { return V(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
static float vlen(v3 a) { return sqrtf(vdot(a,a)); }
static v3 vnorm(v3 a) { return vmul(a, 1.0f/(vlen(a)+1e-9f)); }

static float torus_sdf(v3 p, float t)
{
    float ca = cosf(t*0.6f), sa = sinf(t*0.6f);
    float x =  p.x*ca - p.z*sa;
    float z =  p.x*sa + p.z*ca;
    float y2 = p.y*0.62f - z*0.78f;
    float z2 = p.y*0.78f + z*0.62f;
    float q = sqrtf(x*x + y2*y2) - 1.2f;
    return sqrtf(q*q + z2*z2) - 0.35f;
}

static v3 torus_normal(v3 p, float t)
{
    const float e = 0.002f;
    float d = torus_sdf(p, t);
    return vnorm(V(torus_sdf(vadd(p,V(e,0,0)),t)-d,
                   torus_sdf(vadd(p,V(0,e,0)),t)-d,
                   torus_sdf(vadd(p,V(0,0,e)),t)-d));
}

static float map_all(v3 p, float t, v3 sc, float sr)
{
    float d = p.y + 1.0f;
    float ds = vlen(vsub(p, sc)) - sr;
    if (ds < d) d = ds;
    float dt = torus_sdf(p, t);
    if (dt < d) d = dt;
    return d;
}

static float shadow(v3 ro, v3 rd, float t, v3 sc, float sr)
{
    float tt = 0.03f;
    for (int i = 0; i < 24; i++) {
        float h = map_all(vadd(ro, vmul(rd, tt)), t, sc, sr);
        if (h < 0.002f) return 0.3f;
        tt += h;
        if (tt > 8.0f) break;
    }
    return 1.0f;
}

static uint32_t to888(float r, float g, float b)
{
    if (r<0) r=0;
    if (r>1) r=1;
    if (g<0) g=0;
    if (g>1) g=1;
    if (b<0) b=0;
    if (b>1) b=1;
    return ((uint32_t)(r*255.99f)<<16) | ((uint32_t)(g*255.99f)<<8) | (uint32_t)(b*255.99f);
}

/* 场景求交: 返回最近命中距离, *mat 为材质 (0=天空 1=地面 2=球 3=圆环) */
static float scene_hit(v3 ro, v3 rd, float tmax, int steps,
                       float time, v3 sc, float sr, int *mat)
{
    float th = tmax; int m = 0;
    if (rd.y < -1e-4f) {
        float tp = -(ro.y + 1.0f) / rd.y;
        if (tp > 0.0f && tp < th) { th = tp; m = 1; }
    }
    {
        v3 oc = vsub(ro, sc);
        float bq = vdot(oc, rd);
        float cq = vdot(oc, oc) - sr*sr;
        float disc = bq*bq - cq;
        if (disc > 0.0f) {
            float ts = -bq - sqrtf(disc);
            if (ts > 0.01f && ts < th) { th = ts; m = 2; }
        }
    }
    {
        float tt = 0.02f;
        for (int s = 0; s < steps; s++) {
            float d = torus_sdf(vadd(ro, vmul(rd, tt)), time);
            if (d < 0.001f) { if (tt < th) { th = tt; m = 3; } break; }
            tt += d;
            if (tt > th || tt > 12.0f) break;
        }
    }
    *mat = m;
    return th;
}

/* 反射光线着色: 用低步数二次求交, 非递归 */
static void shade_reflect(v3 pos, v3 n, v3 rd, float time, v3 sc, float sr,
                          v3 ldir, float *rr, float *gg, float *bb)
{
    v3 rdir = vsub(rd, vmul(n, 2.0f * vdot(rd, n)));   /* reflect(rd, n) */
    int m2;
    float t2 = scene_hit(vadd(pos, vmul(n, 0.01f)), rdir, 12.0f, 24, time, sc, sr, &m2);
    if (m2 == 0) {
        /* 天空渐变 + 太阳 */
        float sk = 0.5f + 0.5f * rdir.y;
        if (sk < 0) sk = 0;
        *rr = 0.15f + 0.35f*sk; *gg = 0.25f + 0.45f*sk; *bb = 0.45f + 0.5f*sk;
        float sd = vdot(rdir, ldir);
        if (sd < 0) sd = 0;
        sd = sd*sd*sd*sd;
        *rr += 0.6f*sd; *gg += 0.45f*sd; *bb += 0.2f*sd;
        return;
    }
    v3 p2 = vadd(vadd(pos, vmul(n, 0.01f)), vmul(rdir, t2));
    v3 n2, mc2;
    if (m2 == 1) {
        n2 = V(0,1,0);
        int cx = (int)floorf(p2.x*0.8f), cz = (int)floorf(p2.z*0.8f);
        if ((cx + cz) & 1) mc2 = V(0.9f, 0.9f, 0.95f);
        else               mc2 = V(0.1f, 0.15f, 0.4f);
    } else if (m2 == 2) {
        n2 = vnorm(vsub(p2, sc));
        mc2 = V(1.0f, 0.45f, 0.1f);
    } else {
        n2 = torus_normal(p2, time);
        mc2 = V(0.9f, 0.2f, 0.7f);
    }
    float d2 = vdot(n2, ldir);
    if (d2 < 0) d2 = 0;
    float li2 = 0.25f + 1.4f * d2;
    *rr = mc2.x*li2; *gg = mc2.y*li2; *bb = mc2.z*li2;
    /* 反射点雾 */
    float fo = expf(-t2*0.06f);
    *rr = *rr*fo + 0.2f*(1-fo);
    *gg = *gg*fo + 0.3f*(1-fo);
    *bb = *bb*fo + 0.5f*(1-fo);
}

/* ---------- 并行任务 ---------- */
typedef struct {
    uint32_t *px;
    int w, h, y0, y1;
    float time; int max_steps;
    v3 ro, cu, cv, cw, ldir, sc; float sr;
} job_t;

#define RM_MAX_THREADS 8

static void shade_rows(job_t *J)
{
    int w = J->w, h = J->h;
    for (int j = J->y0; j < J->y1; j++) {
        float v = (h - 2.0f*j) / (float)h;
        for (int i = 0; i < w; i++) {
            float u = (2.0f*i - w) / (float)h;
            v3 rd = vnorm(vadd(vadd(vmul(J->cu,u), vmul(J->cv,v)), vmul(J->cw,1.6f)));

            int mat;
            float t_hit = scene_hit(J->ro, rd, 1e9f, J->max_steps,
                                    J->time, J->sc, J->sr, &mat);

            float r, g, b;
            if (!mat) {
                float sk = 0.5f + 0.5f*rd.y;
                if (sk<0) sk=0;
                r = 0.15f + 0.35f*sk; g = 0.25f + 0.45f*sk; b = 0.45f + 0.5f*sk;
                float sd = vdot(rd, J->ldir);
                if (sd<0) sd=0;
                sd = sd*sd*sd*sd;
                r += 0.6f*sd; g += 0.45f*sd; b += 0.2f*sd;
            } else {
                v3 pos = vadd(J->ro, vmul(rd, t_hit));
                v3 n, mcol;
                if (mat == 1) {
                    n = V(0,1,0);
                    int cx = (int)floorf(pos.x*0.8f), cz = (int)floorf(pos.z*0.8f);
                    if ((cx + cz) & 1) mcol = V(0.9f, 0.9f, 0.95f);
                    else               mcol = V(0.1f, 0.15f, 0.4f);
                } else if (mat == 2) {
                    n = vnorm(vsub(pos, J->sc));
                    mcol = V(1.0f, 0.45f, 0.1f);
                } else {
                    n = torus_normal(pos, J->time);
                    mcol = V(0.9f, 0.2f, 0.7f);
                }
                float dif = vdot(n, J->ldir);
                if (dif<0) dif=0;
                float sh = shadow(vadd(pos, vmul(n,0.02f)), J->ldir, J->time, J->sc, J->sr);
                float spe = vdot(n, vnorm(vsub(J->ldir, rd)));
                if (spe<0) spe=0;
                spe = powf(spe, 16.0f) * sh * 1.5f;   /* 高光增强 */
                float li = 0.25f + 1.4f*dif*sh;
                r = mcol.x*li + spe;
                g = mcol.y*li + spe;
                b = mcol.z*li + spe;
                float fo = expf(-t_hit*0.06f);
                r = r*fo + 0.2f*(1-fo);
                g = g*fo + 0.3f*(1-fo);
                b = b*fo + 0.5f*(1-fo);

                /* 镜面反射: 圆环/球/地面按材质反射系数混合反射色 */
                float ref = (mat == 3) ? 0.5f : (mat == 2) ? 0.35f : 0.15f;
                if (ref > 0.02f) {
                    float rr, gg, bb;
                    shade_reflect(pos, n, rd, J->time, J->sc, J->sr, J->ldir,
                                  &rr, &gg, &bb);
                    r = r*(1.0f-ref) + rr*ref;
                    g = g*(1.0f-ref) + gg*ref;
                    b = b*(1.0f-ref) + bb*ref;
                }
            }
            float dt2 = ((float)(((i*13 + j*7) & 3)) - 1.5f) / 512.0f;
            J->px[j*w + i] = to888(r+dt2, g+dt2, b+dt2);
        }
    }
}

#ifdef _WIN32
/* winpthreads 无 pthread_barrier: 退回每帧 create/join (旧版行为) */
static void *worker(void *a)
{
    shade_rows((job_t *)a);
    return NULL;
}

void raymarch_render(uint32_t *px, int w, int h, float time, int max_steps)
{
    if (max_steps < 8) max_steps = 8;
    float ang = time * 0.35f;

    job_t base;
    base.px = px; base.w = w; base.h = h;
    base.time = time; base.max_steps = max_steps;
    base.ro = V(3.0f*cosf(ang), 1.2f + 0.4f*sinf(time*0.23f), 3.0f*sinf(ang));
    v3 ta = V(0.0f, -0.1f, 0.0f);
    base.cw = vnorm(vsub(ta, base.ro));
    base.cu = vnorm(vcross(base.cw, V(0,1,0)));
    base.cv = vcross(base.cu, base.cw);
    base.ldir = vnorm(V(0.6f, 0.7f, -0.35f));
    base.sc = V(sinf(time*0.9f)*1.6f, 0.2f + 0.4f*sinf(time*1.3f), cosf(time*0.7f)*0.9f);
    base.sr = 0.8f;

    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    if (n > RM_MAX_THREADS) n = RM_MAX_THREADS;
    if (n > h) n = h;
    if (n <= 1) {
        base.y0 = 0; base.y1 = h;
        shade_rows(&base);
        return;
    }
    pthread_t th[RM_MAX_THREADS];
    job_t jobs[RM_MAX_THREADS];
    for (int t = 0; t < n; t++) {
        jobs[t] = base;
        jobs[t].y0 = h * t / n;
        jobs[t].y1 = h * (t + 1) / n;
        pthread_create(&th[t], NULL, worker, &jobs[t]);
    }
    for (int t = 0; t < n; t++) pthread_join(th[t], NULL);
}

#else   /* POSIX: 持久线程池 + barrier */

static pthread_t s_pool[RM_MAX_THREADS];
static pthread_barrier_t s_bar_start, s_bar_done;
static job_t s_jobs[RM_MAX_THREADS];
static volatile int s_pool_n = 0;
static volatile int s_quit = 0;

static volatile int s_pool_go = 0;   /* v2.3.1: barrier 初始化完成前 worker 自旋等待, 消除初始化竞争 */

/* 持久工作线程: 每帧被 barrier 唤醒, 避免反复 pthread_create/join */
static void *rm_worker(void *a)
{
    int id = (int)(intptr_t)a;
    while (!s_pool_go) sched_yield();   /* 等 barrier 就绪/创建结果定型 */
    while (!s_quit) {
        pthread_barrier_wait(&s_bar_start);
        if (s_quit) break;
        shade_rows(&s_jobs[id]);
        pthread_barrier_wait(&s_bar_done);
    }
    return NULL;
}

static int rm_cpu_count(void)
{
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); int n = (int)si.dwNumberOfProcessors;
#else
    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (n < 1) n = 1;
    if (n > RM_MAX_THREADS) n = RM_MAX_THREADS;
    return n;
}

/* 首次调用时固定线程数; 后续帧高度变小也保持, 空分区(y0==y1)无操作 */
static void rm_pool_ensure(int n)
{
    if (s_pool_n != 0) return;
    /* v2.3.1: 逐一检查创建结果 —— 旧代码 pthread_create/barrier_init 失败会让主线程
     * 永久阻塞在 barrier(计数 n+1 缺员)。现按实际创建数定 barrier 计数, 全失败则单线程。 */
    int created = 0;
    for (int i = 0; i < n; i++) {
        if (pthread_create(&s_pool[i], NULL, rm_worker, (void *)(intptr_t)i) != 0) break;
        created++;
    }
    if (created > 0 &&
        pthread_barrier_init(&s_bar_start, NULL, created + 1) == 0 &&
        pthread_barrier_init(&s_bar_done,  NULL, created + 1) == 0) {
        s_pool_n = created;
    } else {
        s_quit = 1;   /* worker 在触碰 barrier 前检查 s_quit, 已创建的直接退出 */
        s_pool_n = -1;
    }
    s_pool_go = 1;
}

void raymarch_render(uint32_t *px, int w, int h, float time, int max_steps)
{
    if (max_steps < 8) max_steps = 8;
    float ang = time * 0.35f;

    job_t base;
    base.px = px; base.w = w; base.h = h;
    base.time = time; base.max_steps = max_steps;
    base.ro = V(3.0f*cosf(ang), 1.2f + 0.4f*sinf(time*0.23f), 3.0f*sinf(ang));
    v3 ta = V(0.0f, -0.1f, 0.0f);
    base.cw = vnorm(vsub(ta, base.ro));
    base.cu = vnorm(vcross(base.cw, V(0,1,0)));
    base.cv = vcross(base.cu, base.cw);
    base.ldir = vnorm(V(0.6f, 0.7f, -0.35f));
    base.sc = V(sinf(time*0.9f)*1.6f, 0.2f + 0.4f*sinf(time*1.3f), cosf(time*0.7f)*0.9f);
    base.sr = 0.8f;

    /* 按行切分, 持久线程池并行 */
    int n = rm_cpu_count();
    if (n <= 1) {
        base.y0 = 0; base.y1 = h;
        shade_rows(&base);
        return;
    }
    rm_pool_ensure(n);
    if (s_pool_n <= 0) {   /* v2.3.1: 线程资源不可用 → 优雅单线程, 不再死锁 */
        base.y0 = 0; base.y1 = h;
        shade_rows(&base);
        return;
    }
    for (int t = 0; t < s_pool_n; t++) {
        s_jobs[t] = base;
        s_jobs[t].y0 = h * t / s_pool_n;
        s_jobs[t].y1 = h * (t + 1) / s_pool_n;
    }
    pthread_barrier_wait(&s_bar_start);   /* 唤醒全部 worker */
    pthread_barrier_wait(&s_bar_done);    /* 等全部完成 */
}

#endif  /* _WIN32 */