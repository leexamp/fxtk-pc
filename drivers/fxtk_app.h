/* ============================================================================
 * fxtk_app.h —— 应用外壳（把 sokol 完全关在驱动侧）
 *
 * 你的应用**只需要两个头**：#include "fxtk.h" + #include "fxtk_app.h"，
 * 然后定义 fxtk_app_init() 建界面即可 —— 不用 include sokol、不用碰驱动结构体、
 * 也不用记得调字体/屏幕初始化（那些容易漏、漏了就没文字或点不动，外壳里已经做掉）。
 *
 *   void fxtk_app_init(void)  必写：建控件
 *   void fxtk_app_frame(void) 选写：每帧回调（动画/定时逻辑）
 *   const char *fxtk_app_title(void)   选写：窗口标题
 *   void fxtk_app_size(int *w, int *h) 选写：初始窗口尺寸
 * ==========================================================================*/
#ifndef FXTK_APP_H
#define FXTK_APP_H

#ifdef __cplusplus
extern "C" {
#endif

void fxtk_app_init(void);                 /* 由你的应用实现 */
void fxtk_app_frame(void);                /* 可选：弱符号，默认空 */
const char *fxtk_app_title(void);         /* 可选：弱符号，默认 "fxtk app" */
void fxtk_app_size(int *w, int *h);       /* 可选：弱符号，默认 640x360 */

#ifdef __cplusplus
}
#endif
#endif /* FXTK_APP_H */
