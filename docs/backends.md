# 后端服务层（fxtk_backends）

> 这一层回答一个问题：**"让 GUI 框架去干跟界面无关的杂活"**。
> 图片怎么解码、剪贴板怎么读写、文件对话框怎么弹、随机数怎么保证可复现——这些每个 GUI 项目都要重写一遍，
> fxtk 把它们收在一处，并按要求分平台给出实现，**没有实现的地方明确报错/回退，不静默假装成功**。

## 分层

```
你的应用            fx_button_new(...) / fx_img_load_file(...) / fx_backend_pick_file(...)
  ↑
框架核心            fxtk.c / fxtk_draw.c / fxtk_widgets.c      ← 只依赖下面两层，不含平台头
  ↑
后端服务层          fxtk_backends.c（本文件主题）
  ↑
驱动抽象            fx_driver_t：矩形批量 / 顶点批 / 文本 / 像素层 / read_pixels / 可选 GPU 钩子
```

**要点**：核心**不含任何平台日志头或系统头**。平台差异只在 `fxtk_backends.c` 里出现一次，
用 `-DFXTK_BACKEND_STUB` / `_WIN32` / `ESP_PLATFORM` / 默认(POSIX) 分支选择实现。

## API 一览

| 类别 | API | 说明 |
|---|---|---|
| 随机 | `fx_rand()` / `fx_rand_seed()` | PCG32；**同种子序列可复现**，测试与演示共用一套随机 |
| 时间 | `fx_time_us()` / `fx_time_ms()` / `fx_time_unix()` | 单调时钟（不受系统时间调整影响）+ 墙钟 |
| 路径 | `fx_path_join()` / `fx_path_dir()` / `fx_path_ext()` / `fx_path_base()` | 跨平台分隔符与扩展名处理 |
| 文件 | `fx_file_read()` / `fx_file_write()` / `fx_file_exists()` | 一次性读写，内部处理大小与错误 |
| 图片 | `fx_img_load_file()` | PNG / JPEG / BMP / GIF / TGA / PNM（stb，已裁掉 HDR/PSD/PIC 省体积） |
| 截图 | `fx_screenshot()` | 驱动 `read_pixels` 回读当前帧 → PNG（stb 编码，零外部依赖） |
| 剪贴板 | `fx_clip_set()` / `fx_clip_get()` | Linux 走 `xsel`/`xclip`/`wl-copy` 并带进程内兜底；Windows 用 `OpenClipboard` + `CF_UNICODETEXT`（UTF-8↔UTF-16）。**驱动侧（sokol）已委托到这两个函数** —— 此前驱动自己用 `system("xsel ...")` 实现，Windows 上必然失败 |
| 交给系统 | `fx_open_url()` / `fx_reveal_file()` | 用默认浏览器打开链接、用文件管理器打开所在目录；**不阻塞、不弹自己的窗口**，POSIX 用 `fork+exec`（不经 shell，避免注入），Windows 用 `ShellExecuteA`，ESP32 明确返回 0 |
| 对话框 | `fx_backend_pick_file()` / `fx_fs_pick_dir()` | Linux 依序尝试 `zenity`→`kdialog`；Win32 用系统对话框 |
| 偏好 | `fx_pref_set()` / `fx_pref_get()` | 轻量键值持久化（配置目录下的文本文件） |
| 能力协商 | `fx_backend_caps()` | 哪些能力在当前平台可用（例如"有对话框吗"），**调用方据此决定回退策略** |
| 日志 | `fx_log()` / `fx_log_set_level()` | 核心统一日志出口；ESP32 侧转发 `esp_log_write`，PC 侧走 stdout/stderr |

## 平台实现矩阵

> ESP32 列仅作历史记录：v2.5.3 起 ESP32 路径停止支持，代码里的纯 CPU 分支保留但不再验证。

| 能力 | Linux | Windows | ESP32（已停用） | stub 后端 |
|---|---|---|---|---|
| 随机 / 时间 / 路径 | ✅ | ✅ | ✅ | ✅（定种子 + 固定时钟） |
| 文件读写 | ✅ | ✅ | ✅ | ✅（内存文件系统） |
| 图片解码 | ✅ | ✅ | ✅ | ✅ |
| PNG 截图 | ✅ | ✅ | 视驱动 | ✅ |
| 剪贴板 | ✅（xsel/xclip/wl-copy） | ✅（OpenClipboard，支持中文） | ❌ | ✅（进程内） |
| 文件/文件夹对话框 | ✅（zenity/kdialog） | ✅（Win32） | ❌ | ❌（返回"取消"并给出提示） |
| 中文输入法 | ✅（XIM，需 sokol 后端） | ❌ **未实现**（需 IMM32） | 视方案 | — |

> **stub 后端**（`-DFXTK_BACKEND_STUB`）是"确定性变体"：固定时钟、定种子随机、内存文件系统。
> `make test` 会用它把整套单元测试**再跑一遍**，所以测试结果不依赖宿主机环境。

## 新增一个后端

1. 在 `fxtk_backends.c` 里加一个 `#elif defined(YOUR_PLATFORM)` 分支，实现上表里的能力
   （用不到的直接 `return 0` / 置空，**不要假装成功**——调用方靠 `fx_backend_caps()` 判断）。
2. 核心不需要改动：它只依赖上表的函数签名。
3. 至少把 `make test` 的真实后端与 `-DFXTK_BACKEND_STUB` 变体各跑一遍；新增分支要能被无头测试覆盖。
   （`tools/esp32_smoke.sh` 是历史存档：ESP32 路径自 v2.5.3 停用，不再随仓库演进。）

## 设计约定

- **不静默降级**：池满、能力缺失、解码失败都走 `fx_log(FX_LOG_WARN/ERROR, ...)` 并返回可判断的失败值。
- **确定性优先**：随机与时间都能被固定（stub 后端），这样金图回归与单元测试才可能逐像素稳定。
- **体积优先**：图片解码裁掉了 HDR/PSD/PIC；需要的格式用 `STBI_NO_*` 精确关闭，而不是整库替换。

## v2.5：多真实窗口后端（FXTK_MW）

除 sokol 单窗口与 SDL 遗留对照外，新增**多窗口**后端（独立编译开关 `-DFXTK_MW`）：
应用通过 `drivers/fxtk_win.h` 的后端无关接口创建/移动/绘制 N 个原生窗口，后端实现为
`drivers/fxtk_sokol_mw.c`（X11 GLX + Win32 WGL；一进程一 GL 上下文、每窗口独立画布、关闭 vsync）。
不定义 `FXTK_MW` 时该后端不参与编译/链接。设计与限制见 [multi-window.md](multi-window.md)。

## 后端服务函数逐条（`fxtk_backends.h`）

### 能力 / 屏幕 / 日志

| 函数 | 用法 |
|---|---|
| `fx_backend_caps()` | 返回能力位（`fx_caps_t`），判断 GPU 圆角/图片/剪贴板等是否可用。 |
| `fx_backend_name()` | 后端名（sokol/sdl/stub），日志/诊断。 |
| `fx_backend_set_screen(w,h,dpi)` | 驱动启动时上报物理屏幕尺寸与 DPI 缩放。 |
| `fx_screen_w()` / `fx_screen_h()` / `fx_dpi_scale()` | 读屏幕像素尺寸 / DPI 缩放。 |
| `fx_ui_scale_for(phys_w,dpi,cap,compact)` | 由物理宽/DPI 计算 UI 缩放（cap=上限%，compact=紧凑）。 |
| `fx_log_set_level(lv)` / `fx_log_get_level()` / `fx_log(lv,fmt,...)` | 日志等级开关 / 取等级 / 打日志。 |

### 随机 / 时间

| 函数 | 用法 |
|---|---|
| `fx_rand_seed(seed)` | 设种子（测试/金图必须固定种子才可复现）。 |
| `fx_rand_u32()` / `fx_rand_below(n)` / `fx_rand_range(lo,hi)` / `fx_randf()` | 32 位随机 / `[0,n)` / `[lo,hi]` 闭区间 / `[0,1)`。 |
| `fx_time_us()` / `fx_time_ms()` | 单调时间（动画计时用，不受墙钟跳变影响）。 |
| `fx_time_unix()` / `fx_sleep_ms(ms)` | Unix 秒 / 睡眠。 |

### 路径 / 文件 / 目录

| 函数 | 用法 |
|---|---|
| `fx_path_join(dst,cap,a,b)` | 拼路径（自动分隔符），返回长度。 |
| `fx_path_basename(path)` / `fx_path_ext(path)` | 取文件名 / 扩展名（静态缓冲，勿 free）。 |
| `fx_path_dir(dst,cap,path)` / `fx_dir_app(dst,cap)` | 取所在目录 / 应用数据目录。 |
| `fx_file_exists(path)` / `fx_file_size(path,&sz)` | 存在性 / 64 位大小。 |
| `fx_file_read(path,&sz)` / `fx_file_free(buf)` | 读整个文件（malloc，失败 NULL）/ 释放。 |
| `fx_file_write(path,data,n,append)` | 写文件（append=1 追加）。 |
| `fx_dir_create(path)` / `fx_dir_list(dir,out,max)` | 建目录 / 列目录（`fx_dir_entry_t` 数组，返回条数）。 |

### 图片 / 剪贴板 / 偏好 / 系统集成

| 函数 | 用法 |
|---|---|
| `fx_img_load_file(path,&img)` / `fx_img_load_mem(data,n,&img)` | 解码 PNG/JPEG/BMP/GIF/TGA/PNM 到 `fx_img_t`。 |
| `fx_img_free(&img)` / `fx_img_has_file_ext(path)` / `fx_img_save_png(...)` | 释放 / 后缀判断 / 存 PNG。 |
| `fx_clip_set(text)` / `fx_clip_get(buf,cap)` | 系统剪贴板写 / 读（返回长度）。 |
| `fx_prefs_set/get` / `fx_prefs_save/load` | 键值偏好设置/读取/落盘/读盘。 |
| `fx_backend_pick_dir(out,cap)` / `fx_backend_pick_file(out,cap,title,exts)` | 系统选目录/文件对话框。 |
| `fx_open_url(url)` / `fx_reveal_file(path)` | 打开浏览器 / 文件管理器定位。 |
| `fx_cpu_count()` | CPU 逻辑核心数（并行默认线程数）。 |

> 完整签名/参数见 [api.md](api.md) 第 15 节。
