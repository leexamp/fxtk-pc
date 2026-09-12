/* spike: 无头 GPU 出图路径验证
 * EGL(device 枚举/surfaceless, 退化为 pbuffer) + 自建 FBO + sokol_gfx(GLES3) → PPM
 * 目的: 证明 v2.4 的 CI 金图回归不需要窗口/显示器, 且 sokol_gfx 能接管我们的渲染管线。
 * 编译: gcc -O2 -I third_party/sokol spike.c -o spike -lEGL -lGLESv2 -lm
 */
#define SOKOL_GLES3
#define SOKOL_GFX_IMPL
#define SOKOL_LOG_IMPL
#include "sokol_gfx.h"
#include "sokol_log.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int SP_W = 256, SP_H = 192;

static const char *vs_src =
    "#version 310 es\n"
    "layout(binding=0) uniform vs_params { vec2 offset; };\n"
    "in vec2 position;\n"
    "in vec4 color0;\n"
    "out vec4 v_color;\n"
    "void main() { v_color = color0; gl_Position = vec4(position + offset, 0.0, 1.0); }\n";
static const char *fs_src =
    "#version 310 es\n"
    "precision mediump float;\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() { frag_color = v_color; }\n";

typedef struct { float offset[2]; } vs_params_t;

static int ppm_write(const char *path, int w, int h)
{
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return 0;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (!f) { free(px); return 0; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)          /* GL 原点在左下, PPM 在左上 */
        for (int x = 0; x < w; x++) {
            unsigned char rgb[3] = { px[(y * w + x) * 4], px[(y * w + x) * 4 + 1], px[(y * w + x) * 4 + 2] };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    free(px);
    return 1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* panic/abort 时也不丢日志 */
    const char *out = argc > 1 ? argv[1] : "/tmp/spike/sokol_spike.ppm";

    /* ---- 1) EGL: 优先硬件 device + surfaceless, 失败退化为 pbuffer ---- */
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    PFNEGLQUERYDEVICESEXTPROC qd = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC gd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (qd && gd) {
        EGLDeviceEXT devs[8]; EGLint n = 0;
        if (qd(8, devs, &n)) {
            for (int i = 0; i < n && dpy == EGL_NO_DISPLAY; i++) {
                EGLDisplay d = gd(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
                if (d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL)) dpy = d;
            }
        }
    }
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg; EGLint ncfg = 0;
    if (dpy == EGL_NO_DISPLAY) {   /* 退化: 默认显示 + pbuffer */
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) { printf("SPIKE-FAIL: EGL 初始化失败\n"); return 1; }
    }
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) { printf("SPIKE-FAIL: chooseConfig\n"); return 1; }
    /* GLES 3.1+ 才原生支持 layout(binding=) 的 uniform block (sokol 着色器约定)。
     * 坑: 用 EGL_CONTEXT_MAJOR/MINOR_VERSION 会被 NVIDIA 当作【桌面 GL】创建(报 GLSL 420 错误),
     * 必须用 CLIENT_VERSION + MINOR_VERSION_KHR 组合。依次 3.2 → 3.1 → 3.0。 */
    EGLContext ctx = EGL_NO_CONTEXT;
    {
        EGLint tries[][2] = { {2, 2}, {2, 1}, {0, 3} };   /* {minor_khr, client_version} */
        for (int i = 0; i < 3 && ctx == EGL_NO_CONTEXT; i++) {
            EGLint attr[7]; int k = 0;
            attr[k++] = EGL_CONTEXT_CLIENT_VERSION; attr[k++] = tries[i][1];
            if (tries[i][0]) { attr[k++] = EGL_CONTEXT_MINOR_VERSION_KHR; attr[k++] = tries[i][0]; }
            attr[k++] = EGL_NONE;
            ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);
            if (ctx != EGL_NO_CONTEXT) printf("SPIKE: 上下文 = ES %d.%d\n", tries[i][1], tries[i][0]);
        }
    }
    if (ctx == EGL_NO_CONTEXT) { printf("SPIKE-FAIL: createContext\n"); return 1; }
    EGLint pb[] = { EGL_WIDTH, SP_W, EGL_HEIGHT, SP_H, EGL_NONE };
    surf = eglCreatePbufferSurface(dpy, cfg, pb);          /* 有它更保险; surfaceless 下为 NO_SURFACE */
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("SPIKE-FAIL: makeCurrent\n"); return 1; }
    printf("SPIKE: GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
    printf("SPIKE: GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    /* ---- 2) 自建 FBO 作为渲染目标 (surfaceless 无默认帧缓冲) ---- */
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SP_W, SP_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("SPIKE-FAIL: FBO 不完整\n"); return 1; }

    /* ---- 3) sokol_gfx 接管: 自带环境(不依赖 sokol_app) ---- */
    sg_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
    desc.logger.func = slog_func;
    sg_setup(&desc);
    if (!sg_isvalid()) { printf("SPIKE-FAIL: sg_setup\n"); return 1; }
    printf("SPIKE: sokol 后端=%d (1=GLES3)\n", (int)sg_query_backend());

    /* ---- 4) 三角形管线 ---- */
    sg_shader_desc shd;
    memset(&shd, 0, sizeof(shd));
    shd.vertex_func.source = vs_src;
    shd.vertex_func.entry = "main";
    shd.fragment_func.source = fs_src;
    shd.fragment_func.entry = "main";
    shd.attrs[0].glsl_name = "position";
    shd.attrs[1].glsl_name = "color0";
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(vs_params_t);
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "offset";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    sg_shader shader = sg_make_shader(&shd);

    float verts[] = {   /* x,y, r,g,b,a */
        -0.6f, -0.5f, 1.0f, 0.3f, 0.2f, 1.0f,
         0.6f, -0.5f, 0.2f, 1.0f, 0.4f, 1.0f,
         0.0f,  0.6f, 0.3f, 0.4f, 1.0f, 1.0f,
    };
    sg_buffer vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts), .label = "spike-vertices" });

    sg_pipeline_desc pd;
    memset(&pd, 0, sizeof(pd));
    pd.shader = shader;
    pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pd.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pd.layout.buffers[0].stride = 6 * sizeof(float);
    sg_pipeline pip = sg_make_pipeline(&pd);
    if (sg_query_pipeline_state(pip) != SG_RESOURCESTATE_VALID) { printf("SPIKE-FAIL: pipeline\n"); return 1; }

    /* ---- 5) 渲染一帧到 FBO ---- */
    sg_pass pass;
    memset(&pass, 0, sizeof(pass));
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = (sg_color){ 0.10f, 0.12f, 0.16f, 1.0f };
    pass.swapchain.width = SP_W;
    pass.swapchain.height = SP_H;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_NONE;
    pass.swapchain.gl.framebuffer = fbo;
    sg_begin_pass(&pass);
    sg_apply_pipeline(pip);
    sg_apply_bindings(&(sg_bindings){ .vertex_buffers[0] = vbuf });
    vs_params_t vp = { { 0.0f, 0.0f } };
    sg_apply_uniforms(0, &(sg_range){ &vp, sizeof(vp) });
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();

    /* ---- 6) 回读 ---- */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (!ppm_write(out, SP_W, SP_H)) { printf("SPIKE-FAIL: 写 PPM\n"); return 1; }
    printf("SPIKE-OK: 已输出 %s (%dx%d)\n", out, SP_W, SP_H);

    sg_shutdown();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    return 0;
}
