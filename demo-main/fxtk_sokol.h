/**
 * fxtk_sokol.h — sokol 平台层公开接口 (v2.4)
 *
 * 供 demo/driver/text 层之间共享:
 *  - fxtk_sokol_tex_t : 纹理句柄 (框架以 void* 传递; 文本层创建, 驱动消费)
 *  - fx_sokol_driver  : fx_driver_t 实现
 *  - fxtk_sokol_frame : 一帧的呈现 (由 sokol_app 的 frame_cb 调用)
 */
#ifndef FXTK_SOKOL_H
#define FXTK_SOKOL_H

#include "sokol_gfx.h"
#include "fxtk.h"

/* 纹理句柄: 文本层(fxtk_font_stb.c)与本驱动共用 */
typedef struct { sg_image img; sg_view view; int w, h; } fxtk_sokol_tex_t;

extern fx_driver_t fx_sokol_driver;

/* 窗口尺寸变化时调用 (事件回调与测试钩子共用) */
void fxtk_sokol_apply_size(int w, int h);
void fxtk_sokol_inject_drag(int x0,int y0,int x1,int y1,int steps);

/* 把本帧累积的顶点命令流提交并呈现 (fx_poll 之后调用) */
void fxtk_sokol_frame(int fb_w, int fb_h);

#endif /* FXTK_SOKOL_H */
