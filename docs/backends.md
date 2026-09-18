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
| 剪贴板 | `fx_clip_set()` / `fx_clip_get()` | Linux 走 `xsel`/`xclip`/`wl-copy` 并带进程内兜底；**Windows 尚未实现** |
| 对话框 | `fx_backend_pick_file()` / `fx_fs_pick_dir()` | Linux 依序尝试 `zenity`→`kdialog`；Win32 用系统对话框 |
| 偏好 | `fx_pref_set()` / `fx_pref_get()` | 轻量键值持久化（配置目录下的文本文件） |
| 能力协商 | `fx_backend_caps()` | 哪些能力在当前平台可用（例如"有对话框吗"），**调用方据此决定回退策略** |
| 日志 | `fx_log()` / `fx_log_set_level()` | 核心统一日志出口；ESP32 侧转发 `esp_log_write`，PC 侧走 stdout/stderr |

## 平台实现矩阵

| 能力 | Linux | Windows | ESP32 | stub 后端 |
|---|---|---|---|---|
| 随机 / 时间 / 路径 | ✅ | ✅ | ✅ | ✅（定种子 + 固定时钟） |
| 文件读写 | ✅ | ✅ | ✅ | ✅（内存文件系统） |
| 图片解码 | ✅ | ✅ | ✅ | ✅ |
| PNG 截图 | ✅ | ✅ | 视驱动 | ✅ |
| 剪贴板 | ✅（xsel/xclip/wl-copy） | ❌ **未实现** | ❌ | ✅（进程内） |
| 文件/文件夹对话框 | ✅（zenity/kdialog） | ✅（Win32） | ❌ | ❌（返回"取消"并给出提示） |
| 中文输入法 | ✅（XIM，需 sokol 后端） | ❌ **未实现**（需 IMM32） | 视方案 | — |

> **stub 后端**（`-DFXTK_BACKEND_STUB`）是"确定性变体"：固定时钟、定种子随机、内存文件系统。
> `make test` 会用它把整套单元测试**再跑一遍**，所以测试结果不依赖宿主机环境。

## 新增一个后端

1. 在 `fxtk_backends.c` 里加一个 `#elif defined(YOUR_PLATFORM)` 分支，实现上表里的能力
   （用不到的直接 `return 0` / 置空，**不要假装成功**——调用方靠 `fx_backend_caps()` 判断）。
2. 核心不需要改动：它只依赖上表的函数签名。
3. 想验证语法与接口是否齐全，跑 `tools/esp32_smoke.sh`（用假头 + `-DESP_PLATFORM` 做语法级检查，不需要真工具链）
   —— 它就是这么发现"ESP32 分支缺 FreeRTOS 头"的。

## 设计约定

- **不静默降级**：池满、能力缺失、解码失败都走 `fx_log(FX_LOG_WARN/ERROR, ...)` 并返回可判断的失败值。
- **确定性优先**：随机与时间都能被固定（stub 后端），这样金图回归与单元测试才可能逐像素稳定。
- **体积优先**：图片解码裁掉了 HDR/PSD/PIC；需要的格式用 `STBI_NO_*` 精确关闭，而不是整库替换。
