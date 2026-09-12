/**
 * fxtk_fs.h — v2.4: 文件/目录能力已并入 fxtk_backends
 *
 * 本头文件保留为【兼容薄壳】, 让 v2.3 的调用点(app_desktop.c / canvas_08_files.c)零改动迁移。
 * 新代码请直接用 fxtk_backends.h 的 fx_dir_list / fx_backend_pick_dir / fx_backend_pick_file。
 * 计划: v2.5 移除本文件。
 */
#ifndef FXTK_FS_H
#define FXTK_FS_H

#include "fxtk_backends.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 兼容旧名 (size 字段由 long 提升为 long long, 与 v2.3 的 Win32 64 位修复一致) */
typedef fx_dir_entry_t fx_fs_entry_t;

/* 弹系统对话框选文件夹, 把绝对路径写入 out (UTF-8)。成功 1, 取消/不可用 0。 */
static inline int fx_fs_pick_dir(char *out, int cap) { return fx_backend_pick_dir(out, cap); }

/* 列目录内容到 out (最多 max 条), 跳过 "." / ".."。返回条目数, 失败 -1。 */
static inline int fx_fs_list(const char *dir, fx_fs_entry_t *out, int max) { return fx_dir_list(dir, out, max); }

#ifdef __cplusplus
}
#endif

#endif /* FXTK_FS_H */
