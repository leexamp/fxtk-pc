#ifndef GPU_RAYMARCH_H
#define GPU_RAYMARCH_H
#include <stdint.h>
/* GPU 光追通道 (独立线程, 不干扰 SDL 的 GL 上下文) */
void gpu_raymarch_start(void);
void gpu_raymarch_shutdown(void);                             /* v2.3.1: 置 g_quit 唤醒 GPU 线程 (atexit 自动注册) */
int  gpu_raymarch_ok(void);                                   /* 1=GPU 可用 (锁内快照) */
const char *gpu_raymarch_renderer(void);                      /* 后端名 (锁内快照) */
void gpu_raymarch_render(uint32_t *px, int w, int h, float time);
#endif
