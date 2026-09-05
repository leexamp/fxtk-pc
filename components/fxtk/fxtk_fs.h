/**
 * fxtk_fs.h — 跨平台文件 API (v2.3 组件页: 文件夹浏览器)
 *
 * 用系统 API 选文件夹 + 列目录内容, 同时支持 Linux(POSIX) 与 Windows(Win32)。
 *  - fx_fs_pick_dir: 系统对话框选文件夹 (Win32 SHBrowseForFolderW / Linux zenity)
 *  - fx_fs_list:     列目录内容 (Win32 FindFirstFileW / POSIX opendir+stat)
 */
#ifndef FXTK_FS_H
#define FXTK_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 目录条目 */
typedef struct {
    char name[256];     /* 文件名/文件夹名 */
    int  is_dir;        /* 1=目录, 0=文件 */
    long size;          /* 文件字节数 */
    char date[32];      /* "YYYY-MM-DD HH:MM" */
} fx_fs_entry_t;

/* 弹系统对话框选文件夹, 把选择的绝对路径写入 out (UTF-8)。返回 1 成功, 0 取消/失败。 */
int fx_fs_pick_dir(char *out, int cap);

/* 列目录内容到 out (最多 max 条), 跳过 "." / ".."。返回条目数, 失败返回 -1。 */
int fx_fs_list(const char *dir, fx_fs_entry_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* FXTK_FS_H */
