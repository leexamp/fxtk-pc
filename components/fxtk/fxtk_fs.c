/**
 * fxtk_fs.c — 跨平台文件 API (v2.3 组件页: 文件夹浏览器)
 *
 * 系统对话框选文件夹 + 列目录内容。Windows 用 Win32(comdlg32/shell32 + FindFirstFileW);
 * Linux 用 zenity(外部命令) 选夹 + POSIX(opendir/stat) 列目录。
 */
#include "fxtk_fs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>
  #include <shlobj.h>      /* SHBrowseForFolderW / SHGetPathFromIDListW */
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #include <time.h>
#endif

/* ================= 系统对话框选文件夹 ================= */
int fx_fs_pick_dir(char *out, int cap)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BROWSEINFOW bi; memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = L"Select a folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) { CoUninitialize(); return 0; }
    wchar_t path[MAX_PATH];
    if (!SHGetPathFromIDListW(pidl, path)) { CoTaskMemFree(pidl); CoUninitialize(); return 0; }
    /* v2.3.1: 检查转换结果 — 截断(返回0)时 out 可能未写, 旧代码误报成功 */
    int wn = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL);
    CoTaskMemFree(pidl);
    CoUninitialize();
    return (wn > 0 && out[0]) ? 1 : 0;
#else
    /* Linux: 用 zenity (多数桌面自带); 没有则回退为空 */
    FILE *p = popen("zenity --file-selection --directory --title='Select a folder' 2>/dev/null", "r");
    if (!p) return 0;
    char *r = fgets(out, cap, p);
    pclose(p);
    if (!r) { out[0] = 0; return 0; }
    size_t n = strlen(out);
    while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
    return out[0] ? 1 : 0;
#endif
}

/* ================= 列目录内容 ================= */
int fx_fs_list(const char *dir, fx_fs_entry_t *out, int max)
{
    if (!dir || !out || max <= 0) return -1;
#ifdef _WIN32
    /* v2.3.1: 检查转换并显式限长拼接 — 旧代码转换失败时 pattern 未初始化,
     * 且 wcscat 无界拼接, 长目录名即栈溢出 */
    wchar_t pattern[1024];
    int dn = MultiByteToWideChar(CP_UTF8, 0, dir, -1, pattern, 1024);
    if (dn <= 0 || dn > 1020) return -1;
    pattern[dn-1] = L'\\'; pattern[dn] = L'*'; pattern[dn+1] = L'\0';
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    do {
        if (fd.cFileName[0] == L'.') continue;   /* 跳过 . .. 及隐藏 */
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, out[n].name, sizeof(out[n].name), NULL, NULL)) continue;
        out[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        out[n].size   = (long)(((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);   /* v2.3.1: 64 位大小, 旧代码丢高 32 位 */
        SYSTEMTIME st; FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
        snprintf(out[n].date, sizeof(out[n].date), "%04d-%02d-%02d %02d:%02d",
                 (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute);
        n++;
    } while (n < max && FindNextFileW(h, &fd));
    FindClose(h);
    return n;
#else
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;       /* 跳过 . .. 及隐藏 */
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
        strncpy(out[n].name, e->d_name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = 0;
        out[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        out[n].size   = (long)st.st_size;
        struct tm tmv; localtime_r(&st.st_mtime, &tmv);
        strftime(out[n].date, sizeof(out[n].date), "%Y-%m-%d %H:%M", &tmv);
        n++;
    }
    closedir(d);
    return n;
#endif
}
