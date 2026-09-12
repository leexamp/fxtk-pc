/**
 * gpu_raymarch_stub.c — v2.4 sokol 路径的 GPU 光追桩
 *
 * 旧的"独立 EGL 上下文 + 独立线程"通道在部分 NVIDIA 驱动上会崩在驱动内部, 且与 sokol 的
 * 单上下文模型冲突。sokol 路径下光追将由同管线的 render pass 实现(P4), 在那之前这里提供
 * 明确的 CPU 回退桩: ok=0 → 演示自动走 CPU 光线步进。
 */
#include <stdint.h>

void gpu_raymarch_start(void) {}
int  gpu_raymarch_ok(void) { return 0; }
const char *gpu_raymarch_renderer(void) { return "CPU"; }
void gpu_raymarch_render(uint32_t *px, int w, int h, float time) { (void)px; (void)w; (void)h; (void)time; }
