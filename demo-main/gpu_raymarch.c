/**
 * gpu_raymarch.c — GPU 光追通道 v2
 * 【v2】拒绝 llvmpipe/softpipe; 默认显示失败时枚举 EGL 硬件设备;
 *      FPS 标签显示真实后端, 不再撒谎。
 */
#define _GNU_SOURCE   /* v2.3: pthread_timedjoin_np */
#include "gpu_raymarch.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *VERT_SRC =
"attribute vec2 a_pos;\n"
"void main(){ gl_Position = vec4(a_pos,0.0,1.0); }\n";

static const char *FRAG_SRC =
"precision highp float;\n"
"uniform vec2 u_res;\n"
"uniform float u_time;\n"
"float sdTorus(vec3 p){\n"
"  float ca=cos(u_time*0.6), sa=sin(u_time*0.6);\n"
"  float x=p.x*ca-p.z*sa; float z=p.x*sa+p.z*ca;\n"
"  float y2=p.y*0.62-z*0.78; float z2=p.y*0.78+z*0.62;\n"
"  float q=length(vec2(x,y2))-1.2;\n"
"  return length(vec2(q,z2))-0.35;\n"
"}\n"
"vec3 sphCenter(float t){ return vec3(sin(t*0.9)*1.6, 0.2+0.4*sin(t*1.3), cos(t*0.7)*0.9); }\n"
"float map(vec3 p){\n"
"  float d=p.y+1.0;\n"
"  d=min(d, length(p-sphCenter(u_time))-0.8);\n"
"  return min(d, sdTorus(p));\n"
"}\n"
"vec3 nrm(vec3 p){\n"
"  vec2 e=vec2(0.002,0.0);\n"
"  return normalize(vec3(map(p+e.xyy)-map(p-e.xyy),\n"
"                        map(p+e.yxy)-map(p-e.yxy),\n"
"                        map(p+e.yyx)-map(p-e.yyx)));\n"
"}\n"
"float shadow(vec3 ro, vec3 rd){\n"
"  float t=0.03;\n"
"  for(int i=0;i<24;i++){ float h=map(ro+rd*t); if(h<0.002) return 0.3; t+=h; if(t>8.0) break; }\n"
"  return 1.0;\n"
"}\n"
"/* 场景求交: 输出 th(mat), 返回最近 t (0=天空 1=地面 2=球 3=圆环) */\n"
"float sceneHit(vec3 ro, vec3 rd, float tmax, int steps, out float th, out int mat){\n"
"  float t=tmax; int m=0;\n"
"  if(rd.y<-1e-4){ float tp=-(ro.y+1.0)/rd.y; if(tp>0.0 && tp<t){ t=tp; m=1; } }\n"
"  vec3 sc=sphCenter(u_time);\n"
"  vec3 oc=ro-sc; float bq=dot(oc,rd); float cq=dot(oc,oc)-0.64; float disc=bq*bq-cq;\n"
"  if(disc>0.0){ float ts=-bq-sqrt(disc); if(ts>0.01 && ts<t){ t=ts; m=2; } }\n"
"  float tt=0.02;\n"
"  for(int i=0;i<steps;i++){ float d=sdTorus(ro+rd*tt);\n"
"    if(d<0.001){ if(tt<t){ t=tt; m=3; } break; }\n"
"    tt+=d; if(tt>t || tt>12.0) break; }\n"
"  th=t; mat=m; return t;\n"
"}\n"
"/* 反射光线着色 (非递归, 低步数) */\n"
"vec3 shadeReflect(vec3 pos, vec3 n, vec3 rd, vec3 L){\n"
"  vec3 rdir=reflect(rd, n);\n"
"  float th; int mat;\n"
"  sceneHit(pos+n*0.01, rdir, 12.0, 24, th, mat);\n"
"  if(mat==0){\n"
"    float sk=clamp(0.5+0.5*rdir.y,0.0,1.0);\n"
"    vec3 c=vec3(0.15+0.35*sk,0.25+0.45*sk,0.45+0.5*sk);\n"
"    c+=vec3(0.6,0.45,0.2)*pow(max(dot(rdir,L),0.0),4.0);\n"
"    return c;\n"
"  }\n"
"  vec3 p2=pos+n*0.01+rdir*th;\n"
"  vec3 n2, m2;\n"
"  if(mat==1){ n2=vec3(0.0,1.0,0.0);\n"
"    vec2 c=floor(p2.xz*0.8);\n"
"    m2=(mod(c.x+c.y,2.0)<1.0)?vec3(0.9,0.9,0.95):vec3(0.1,0.15,0.4);\n"
"  } else if(mat==2){ n2=normalize(p2-sphCenter(u_time)); m2=vec3(1.0,0.45,0.1); }\n"
"  else { n2=nrm(p2); m2=vec3(0.9,0.2,0.7); }\n"
"  float d=max(dot(n2,L),0.0);\n"
"  vec3 c=m2*(0.25+1.4*d);\n"
"  return mix(vec3(0.2,0.3,0.5), c, exp(-th*0.06));\n"
"}\n"
"void main(){\n"
"  float t=u_time;\n"
"  vec3 ro=vec3(3.0*cos(t*0.35), 1.2+0.4*sin(t*0.23), 3.0*sin(t*0.35));\n"
"  vec3 cw=normalize(vec3(0.0,-0.1,0.0)-ro);\n"
"  vec3 cu=normalize(cross(cw, vec3(0.0,1.0,0.0)));\n"
"  vec3 cv=cross(cu,cw);\n"
"  vec2 uv=(2.0*gl_FragCoord.xy-u_res)/u_res.y;\n"
"  vec3 rd=normalize(uv.x*cu+uv.y*cv+1.6*cw);\n"
"  vec3 L=normalize(vec3(0.6,0.7,-0.35));\n"
"  float th; int mat;\n"
"  sceneHit(ro, rd, 1e9, 96, th, mat);\n"
"  vec3 col;\n"
"  if(mat==0){\n"
"    float sk=clamp(0.5+0.5*rd.y,0.0,1.0);\n"
"    col=vec3(0.15+0.35*sk,0.25+0.45*sk,0.45+0.5*sk);\n"
"    col+=vec3(0.6,0.45,0.2)*pow(max(dot(rd,L),0.0),4.0);\n"
"  } else {\n"
"    vec3 pos=ro+rd*th; vec3 n; vec3 m;\n"
"    if(mat==1){ n=vec3(0.0,1.0,0.0);\n"
"      vec2 c=floor(pos.xz*0.8);\n"
"      m=(mod(c.x+c.y,2.0)<1.0)?vec3(0.9,0.9,0.95):vec3(0.1,0.15,0.4);\n"
"    } else if(mat==2){ n=normalize(pos-sphCenter(u_time)); m=vec3(1.0,0.45,0.1); }\n"
"    else { n=nrm(pos); m=vec3(0.9,0.2,0.7); }\n"
"    float dif=max(dot(n,L),0.0);\n"
"    float sh=(dif>0.02)?shadow(pos+n*0.02,L):0.0;\n"
"    float spe=pow(max(dot(n,normalize(L-rd)),0.0),16.0)*sh*1.5;\n"
"    col=m*(0.25+1.4*dif*sh)+vec3(spe);\n"
"    col=mix(vec3(0.2,0.3,0.5), col, exp(-th*0.06));\n"
"    /* 镜面反射: 圆环0.5 球0.35 地面0.15 */\n"
"    float ref=(mat==3)?0.5:((mat==2)?0.35:0.15);\n"
"    col=mix(col, shadeReflect(pos, n, rd, L), ref);\n"
"  }\n"
"  gl_FragColor=vec4(clamp(col,0.0,1.0),1.0);\n"
"}\n";

/* ---------------- 任务队列 ---------------- */
typedef struct { int w, h; float time; uint32_t *dst; } job_t;
static pthread_t g_th;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_creq = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_cdone = PTHREAD_COND_INITIALIZER;
static long g_req = 0, g_done = 0;
static job_t g_job;
static int g_ok = -1;
static int g_started = 0;
static int g_quit = 0;
static char g_renderer[128] = "GPU";

/* ---------------- EGL/GL ---------------- */
static EGLDisplay g_egl; static EGLContext g_ctx; static EGLSurface g_surf;
static EGLConfig g_cfg;
static int g_surf_w = 0, g_surf_h = 0;
static GLuint g_prog, g_vbo;
static GLint g_u_res, g_u_time;
static uint8_t *g_rb = NULL; static int g_rb_cap = 0;

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); printf("[gpu] shader: %s\n", log); }
    return ok ? s : 0;
}

/* 在已初始化的 display 上建上下文; 拒绝软件 GL */
static int init_gl_on(EGLDisplay d)
{
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &cfg, 1, &n) || n < 1) { printf("[gpu] chooseConfig fail n=%d\n", n); return 0; }
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
    if (c == EGL_NO_CONTEXT) { printf("[gpu] createContext fail\n"); return 0; }
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) { printf("[gpu] makeCurrent(surfaceless) fail\n"); return 0; }
    const char *r = (const char *)glGetString(GL_RENDERER);
    if (!r) return 0;
    if (strstr(r, "llvmpipe") || strstr(r, "softpipe") ||
        strstr(r, "swrast")  || strstr(r, "Software")) {
        printf("[gpu] refuse software GL: %s\n", r);
        return 0;
    }
    g_egl = d; g_ctx = c; g_cfg = cfg;
    snprintf(g_renderer, sizeof(g_renderer), "%s", r);

    GLuint vs = compile(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs); glAttachShader(g_prog, fs);
    glDeleteShader(vs); glDeleteShader(fs);   /* 链接后即可释放 */
    glBindAttribLocation(g_prog, 0, "a_pos");
    glLinkProgram(g_prog);
    GLint ok = 0; glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { printf("[gpu] link fail\n"); return 0; }
    g_u_res = glGetUniformLocation(g_prog, "u_res");
    g_u_time = glGetUniformLocation(g_prog, "u_time");
    static const float tri[] = { -1,-1, 3,-1, -1,3 };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    return 1;
}

static int gl_setup(void)
{
    /* 1) 优先枚举 EGL 硬件设备 (surfaceless, 不碰 Xlib, 子线程安全) */
    PFNEGLQUERYDEVICESEXTPROC qd =
        (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC gd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (qd && gd) {
        EGLDeviceEXT devs[8]; EGLint n = 0;
        if (qd(8, devs, &n)) {
            printf("[gpu] %d EGL device(s) found\n", n);
            for (int i = 0; i < n; i++) {
                EGLDisplay dd = gd(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
                if (dd == EGL_NO_DISPLAY) continue;
                if (!eglInitialize(dd, NULL, NULL)) continue;
                if (init_gl_on(dd)) return 1;
                printf("[gpu] dev%d init_gl_on failed\n", i);
                eglTerminate(dd);
            }
        }
    } else {
        printf("[gpu] no eglQueryDevicesEXT, try default display\n");
    }
    /* 2) 显式 X11 EGL (与 SDL 同源硬件链路) */
    {
        Display *xd = XOpenDisplay(NULL);
        if (xd) {
            EGLDisplay dx = eglGetDisplay((EGLNativeDisplayType)xd);
            printf("[gpu] X11 EGL display=%p\n", (void*)dx);
            if (dx != EGL_NO_DISPLAY && eglInitialize(dx, NULL, NULL)) {
                if (init_gl_on(dx)) return 1;
                eglTerminate(dx);
            }
            XCloseDisplay(xd);
        } else printf("[gpu] XOpenDisplay failed\n");
    }
    /* 3) 回退默认显示 */
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL)) {
        if (init_gl_on(d)) return 1;
        eglTerminate(d);
    }
    printf("[gpu] no hardware GL available, stay on CPU\n");
    return 0;
}

static int ensure_surface(int w, int h)
{
    if (g_surf && g_surf_w == w && g_surf_h == h)
        return eglMakeCurrent(g_egl, g_surf, g_surf, g_ctx);
    if (g_surf) eglDestroySurface(g_egl, g_surf);
    EGLint pb[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
    g_surf = eglCreatePbufferSurface(g_egl, g_cfg, pb);
    if (g_surf == EGL_NO_SURFACE) { printf("[gpu] pbuffer failed\n"); return 0; }
    g_surf_w = w; g_surf_h = h;
    return eglMakeCurrent(g_egl, g_surf, g_surf, g_ctx);
}

static void do_render(job_t *j)
{
    if (!ensure_surface(j->w, j->h)) return;
    glViewport(0, 0, j->w, j->h);
    glUseProgram(g_prog);
    glUniform2f(g_u_res, (float)j->w, (float)j->h);
    glUniform1f(g_u_time, j->time);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    int need = j->w * j->h * 4;
    if (g_rb_cap < need) { free(g_rb); g_rb = (uint8_t *)malloc(need); g_rb_cap = need; }
    if (!g_rb) return;
    glReadPixels(0, 0, j->w, j->h, GL_RGBA, GL_UNSIGNED_BYTE, g_rb);

    for (int y = 0; y < j->h; y++) {
        const uint8_t *src = g_rb + (size_t)(j->h - 1 - y) * j->w * 4;
        uint32_t *dst = j->dst + (size_t)y * j->w;
        for (int x = 0; x < j->w; x++) {
            uint8_t r = src[x*4], g = src[x*4+1], b = src[x*4+2];
            dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void *gpu_thread(void *a)
{
    (void)a;
    int setup = gl_setup();
    pthread_mutex_lock(&g_mtx);
    g_ok = setup ? 1 : 0;
    if (!setup) { g_done = g_req; pthread_cond_broadcast(&g_cdone); }
    pthread_mutex_unlock(&g_mtx);
    if (!setup) return NULL;
    printf("[gpu] raymarch ready on: %s\n", g_renderer);

    for (;;) {
        pthread_mutex_lock(&g_mtx);
        while (g_done == g_req && !g_quit) pthread_cond_wait(&g_creq, &g_mtx);
        if (g_quit) { pthread_mutex_unlock(&g_mtx); break; }
        job_t j = g_job; long seq = g_req;
        pthread_mutex_unlock(&g_mtx);
        do_render(&j);
        pthread_mutex_lock(&g_mtx);
        g_done = seq;
        pthread_cond_broadcast(&g_cdone);
        pthread_mutex_unlock(&g_mtx);
    }
    return NULL;
}

int gpu_raymarch_ok(void) {
    pthread_mutex_lock(&g_mtx);   /* v2.3.1: g_ok 由 GPU 线程在锁内写, 旧代码无锁读 = 数据竞争 UB */
    int ok = (g_ok == 1);
    pthread_mutex_unlock(&g_mtx);
    return ok;
}
const char *gpu_raymarch_renderer(void) {
    static char buf[128];
    pthread_mutex_lock(&g_mtx);
    snprintf(buf, sizeof(buf), "%s", g_renderer);
    pthread_mutex_unlock(&g_mtx);
    return buf;
}

void gpu_raymarch_start(void)
{
    /* v2.4: 该通道是"独立 EGL 上下文 + 独立线程"的实验性实现, 在某些 NVIDIA 驱动上
     * EGL 设备枚举/初始化会直接崩在驱动内部 (libnvidia-glsi/libEGL_nvidia, 实测 SIGSEGV,
     * 无法在用户态捕获)。v2.4 的 sokol 后端会把光追改成同一上下文的 render pass 从而
     * 彻底移除本通道; 在那之前默认【关闭】, 需要时用 FXTK_GPU=1 显式开启。 */
    if (!getenv("FXTK_GPU")) return;
    if (!g_started) {
        g_started = 1;
        atexit(gpu_raymarch_shutdown);   /* v2.3.1: 退出时唤醒 GPU 线程, 不再仅靠 exit(0) 硬杀 */
        pthread_create(&g_th, NULL, gpu_thread, NULL);
    }
}
void gpu_raymarch_shutdown(void)
{
    static int s_joined = 0;
    if (s_joined || !g_started) return;
    s_joined = 1;
    pthread_mutex_lock(&g_mtx);
    g_quit = 1;
    pthread_cond_broadcast(&g_creq);
    pthread_mutex_unlock(&g_mtx);
    /* v2.3 关键修复: 必须【等】GPU 线程真正退出 EGL 后再让进程继续拆 SDL/驱动。
     * 旧实现只置 g_quit 就返回 —— 本 atexit 处理器在 SDL_Quit 之前运行(LIFO), 于是
     * 主线程拆 SDL/GL 的同时 GPU 线程还在驱动内部用 EGL, 驱动的小块堆被破坏, 表现为退出时
     * glibc 报 "corrupted size vs. prev_size in fastbins"(约 25% 概率, 随负载升高)。
     * 该损坏在驱动库内部, ASan/TSan 均不可见; gdb 拖慢时序也会掩盖它。 */
    if (!pthread_equal(g_th, pthread_self())) {
        /* 限时 join: 正常情况 GPU 线程毫秒级退出; 万一卡在驱动内部, 最多等 2s 就走,
         * 绝不把关窗口变成挂死。 */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (pthread_timedjoin_np(g_th, NULL, &ts) != 0)
            fprintf(stderr, "[gpu] shutdown: join timeout (驱动内部卡住?), 直接退出\n");
    }
}
void gpu_raymarch_render(uint32_t *px, int w, int h, float time)
{
    gpu_raymarch_start();
    if (!gpu_raymarch_ok()) return;   /* v2.3.1: 锁内读 g_ok */
    pthread_mutex_lock(&g_mtx);
    g_job.w = w; g_job.h = h; g_job.time = time; g_job.dst = px;
    long seq = ++g_req;
    pthread_cond_signal(&g_creq);
    while (g_done != seq) pthread_cond_wait(&g_cdone, &g_mtx);
    pthread_mutex_unlock(&g_mtx);
}
