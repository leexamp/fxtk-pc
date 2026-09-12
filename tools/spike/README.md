# P0 spike 结论：sokol 后端可行性（v2.4）

> 结论：**可行**，两个平台都已编译链接验证，无头出图路径已跑通。
> vendored 版本记录在 `third_party/sokol/VERSION.txt`（commit 70bb7c5）与 `third_party/stb/VERSION.txt`。

## 验证结果

| 项 | 结果 |
|---|---|
| Linux 无头出图（无窗口/无显示器） | ✅ EGL device 枚举 → GLES 3.2 上下文 → 自建 FBO → sokol_gfx(GLES3) 渲染三角形 → `glReadPixels` → PPM |
| Linux 窗口路径（sokol_app + GLCORE） | ✅ 编译链接 373 KB；真实窗口运行 60 帧干净退出，`sg_query_backend()=GLCORE` |
| Windows 交叉编译（sokol_app + D3D11） | ✅ mingw 编译链接 523 KB PE32+，仅依赖系统 DLL（d3d11/dxgi/d3dcompiler_47） |
| PPM → PNG 工具（截图/金图用） | ✅ `tools/ppm2png.c`（基于 vendored stb_image_write，零外部依赖） |

## 踩过的坑（重要，别再踩）

1. **HTTP 层面的教训先记一条**：`third_party/` 在 `demo-main/` 下（SDL2 Windows 开发包），
   而 sokol/stb 这类"框架级 vendor"放**顶层** `third_party/`；两者在 `.gitignore` 里分别处理
   （前者忽略，后者提交，保证 CI 不需要联网拉依赖）。
2. **EGL 上下文类型**：用 `EGL_CONTEXT_MAJOR_VERSION`/`MINOR_VERSION` 请求会拿到**桌面 GL**
   （NVIDIA 报 `layout qualifier 'binding' requires "#version 420"`），必须用
   `EGL_CONTEXT_CLIENT_VERSION=3` + `EGL_CONTEXT_MINOR_VERSION_KHR=2` 组合才是 GLES。
3. **GLSL 版本**：sokol 的着色器约定要求 uniform block 带 `layout(binding=N)`，这在
   `#version 300 es`（GLES 3.0）里**不合法**，必须 `#version 310 es` 及以上（配 GLES 3.1+ 上下文）。
4. **uniform block 名**：sokol 按名字查块（`vs_params`/`fs_params`），驱动可能优化掉未使用的成员并给出
   `GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER` 警告——不影响运行，但写 shader 时要保证块名与
   `sg_shader_desc.uniform_blocks[]` 的 `glsl_uniforms[].glsl_name` 一致。
5. **surfaceless 无默认帧缓冲**：sokol_gfx 需要 `sg_swapchain.gl.framebuffer`，无头场景必须自建
   FBO + 颜色纹理并传入其句柄，渲染后用 `glReadPixels` 回读（GL 原点在左下，输出 PPM/PNG 要翻转）。
6. **stdout 缓冲**：sokol 校验失败会 panic/abort，行缓冲会丢日志——spike 里用
   `setvbuf(stdout, NULL, _IONBF, 0)`，后续驱动的诊断输出也应如此。
7. **mingw 链接**：D3D11 路径需要 `-ld3d11 -ldxgi -lole32 -luuid -lgdi32 -luser32 -lshell32`；
   Linux 窗口路径需要 `-lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm`。

## 后续（P2 实施要点）

- `fx_driver_t` 的**轮询契约保持不变**（ESP32 与无头测试都依赖它）：sokol_app 的回调事件入环形队列，
  由 `touch_read/key_read/wheel_read` 出队。
- 光追 GPU 通道从"独立 EGL 线程 + 独立上下文"改为同管线的 render-to-texture pass，
  **双上下文 hack 与其竞态整类消失**（v2.3 修过的退出期堆损坏即源于此）。
- 无头出图路径直接作为 CI 金图回归的渲染后端（不需要 xvfb/显示器）。
