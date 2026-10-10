/* 仅用于 window_test: sokol_app 的实现必须【恰好在一个 TU】里, 所以单开这个文件,
 * 避免把整个 fxtk_sokol_driver.c(1500 行 + 光追)拖进窗口行为测试。 */
#define SOKOL_GLCORE
#define SOKOL_APP_IMPL
#define SOKOL_LOG_IMPL
#include "sokol_app.h"
#include "sokol_log.h"
