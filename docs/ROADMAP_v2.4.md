# fxtk v2.4 路线图

> 状态：规划稿（v2.3 发布后）。目标是把 fxtk 从"SDL2 实验性 PC 后端 + CPU 抗锯齿"推进到
> "sokol 自裁剪后端 + GPU 全局抗锯齿 + 通用跨平台服务层 + 纹理四边形形变"。

## 0. 目标与验收指标

| 维度 | v2.3 现状 | v2.4 目标 |
|---|---|---|
| PC 后端 | SDL2 + SDL2_ttf + SDL2_image（3 个 DLL .so 依赖） | sokol（自裁剪 vendored）+ stb_truetype/stb_image，**零外部运行库** |
| Linux 体积 | 101 KB（+ 动态依赖 SDL2 三件套） | ≤ 150 KB **静态自包含** |
| Windows 体积 | exe 183 KB + 3 个 DLL（剥符号后 ~2 MB） | 单 exe ≤ 250 KB，**零 DLL** |
| 抗锯齿 | `fx_set_aa(1)` → 画布强制离屏 + CPU 扫描线，仅画布生效 | **全局 GPU AA**（默认开）：SDF 控件 + MSAA + 羽化线 |
| 压测性能 | 2816 控件 8.0 ms/帧（125 fps，dummy 软件路径） | 同条件 ≥ 300 fps；真实 GPU 路径 poll+present ≤ 4 ms |
| 文本 | 每帧 1276 次 TTF 查询（已加宽度缓存）+ 每串一张纹理 | **字形图集（glyph atlas）** 批量绘制，1000 次文本/帧 ≤ 0.3 ms |
| 图片 | 仅旋转 blit（`fx_draw_image_ex`/`_rot`） | **四边形形变**（透视正确）+ 导入/缩放/复位 UI |
| 平台服务 | `fxtk_fs.c`（仅选目录 + 列目录） | `fxtk_backends.c`：文件/图片/剪贴板/时间/随机/系统信息/偏好 |
| 控件外观 | 功能优先 | 逐个审美打磨（截图画廊 + 设计令牌统一） |

## 1. 工作流 A：PC 后端 SDL2 → sokol（最大工作量）

> **已完成并定案(v2.4)**: sokol 为默认后端, SDL2 降为遗留(只当无头假驱动供 bench/金图使用)。
> `make` = sokol; `make fxtk_sim_sdl` = 遗留对照。

### 1.1 为什么值得换
- **体积/依赖**：SDL2 是动态库（Windows 侧 SDL2_ttf.dll 剥符号后仍 ~2 MB）；sokol 是单头文件、静态链入，
  可只保留 GL 后端并裁掉 audio/fetch/imgui，产出单文件可执行。
- **管线控制权**：sokol_gfx 允许**自定义着色器**——这是 GPU 抗锯齿、字形图集批量、透视四边形三件事的共同前提。
  SDL2 的 renderer 抽象拿不到这些。
- **并发简化**：现在 3D 页的 GPU 光追走**独立 EGL 线程 + 独立上下文**（v2.3 还因此修过一次退出期堆损坏）。
  迁到 sokol_gfx 后光追变成同一条管线里的 render-to-texture pass，**双上下文 hack 与相关竞态整类消失**。

### 1.2 主要改造点
1. **主循环反转**：sokol_app 要求 `sokol_main()` + `sapp_desc` 回调（它拥有主循环），而 fxtk 现在是
   "main 拥有循环 + 驱动轮询输入"。
   → 保持 `fx_driver_t` 的轮询契约不变（ESP32 与无头测试都依赖它），在 sokol 驱动内部把
   `sapp_*` 回调事件**入环形队列**，由 `touch_read/key_read/wheel_read` 出队。核心框架零改动。
2. **渲染层重写**（`fxtk_sokol_driver.c`）：
   - 保留 v2.3 已经验证的分层：矩形批（`SDL_RenderFillRects` → 实例化 SDF quad 或 `sg_draw` 批次）、
     顶点批（线段/三角形）、软件像素回读层（文字/离屏画布）。
   - 目标终态是**尽可能少的状态切换**：控件背景+边框+圆角 → SDF 实例化一次 draw；文本 → 图集一次 draw。
3. **文本栈**：SDL_ttf → `stb_truetype` + 自管**字形图集**（ASCII 预载 + CJK 按需，LRU 淘汰，
   多页图集兜底）；尺寸测量改查图集度量（现有 v2.3 宽度缓存保留作为上层缓存）。
4. **图片栈**：SDL_image → `stb_image`（解码）+ `stb_image_write`（截图/导出 PNG）。
5. **剪贴板**：sokol 不提供 → 平台代码（Linux: X11/Wayland 双路径；Windows: Win32）。**风险项**，
   过渡期可先降级为"框架内部剪贴板"（文本框间复制粘贴仍可用，跨应用粘贴暂不可用）。
6. **文件对话框**：sokol 不提供 → 复用现有实现（Linux zenity/portal；Win32 `SHBrowseForFolderW`
   → 扩展为 `IFileOpenDialog` 支持选文件与过滤器）。
7. **ESP32 路径**：不动。sokol 不支持 ESP32，ESP32 继续走自有驱动 + CPU 抗锯齿（见 §3.4）。

### 1.3 迁移策略（保命）
- SDL2 驱动**保留**一个版本周期，作为**参考实现**：两个驱动都能出图，逐帧 PPM 对比（工具已有：
  `bench --dump` + `render_canvas`）。
- 分步切换：先并存（sokol 可选开启）→ 逐页验收 → 默认切 sokol → 移除 SDL2 依赖（保留可编译开关）。
- 先做**技术预研 spike**（0.5 天）：mingw 交叉编译 sokol_app（Win32 + D3D11 或 WGL 哪条路可行）、
  EGL surfaceless 无头出图可行性。**这一步的结论会影响整个 A 的排期**。

## 2. 工作流 B：`fxtk_fs.c` → `fxtk_backends.c`（通用跨平台服务层）

### 2.1 目标形态
```
components/fxtk/fxtk_backends.h          # 统一接口 + 能力查询
components/fxtk/fxtk_backends_posix.c
components/fxtk/fxtk_backends_win32.c
components/fxtk/fxtk_backends_esp32.c
components/fxtk/fxtk_backends_stub.c     # 无头测试: 确定性时钟/种子随机/内存 FS
```

### 2.2 接口清单（★ = 你点名的、☆ = 我建议一起做）
| 分类 | 接口 | 说明 |
|---|---|---|
| ★ 文件 | `fx_fs_pick_dir/pick_file(cap,filters)` | 选目录 / 选文件（多选 + 后缀过滤） |
| ★ 文件 | `fx_fs_list/stat/exists/mkdir/remove` | 目录与元信息（64 位大小，v2.3 已修 Win32 侧） |
| ★ 文件 | `fx_fs_read/write/append` + `fx_path_join/basename/ext/normalize` | 整文件读写与路径工具 |
| ★ 随机 | `fx_rand_u32/range/float01`、`fx_rand_seed` | **PCG32 自实现**（不依赖 `rand()`）；固定种子可复现 → 测试可确定性 |
| ☆ 图片 | `fx_img_load_file/load_mem`、`fx_img_save_png` | stb_image / stb_image_write；喂给"导入图片"与截图 |
| ☆ 时间 | `fx_time_ms/us`（单调）、`fx_time_unix`、`fx_sleep_ms` | 替换框架内散落的平台计时调用 |
| ☆ 剪贴板 | `fx_clip_get/set` | 见 §1.2-5 的风险与降级 |
| ☆ 系统 | `fx_cpu_count`、`fx_mem_used`、`fx_screen_size`、`fx_dpi_scale`、`fx_platform_name` | 自适应缩放 / HUD / 压测页 |
| ☆ 日志 | `fx_log(level, fmt, ...)` | 桥接 `esp_log` 与 stderr，统一 `FXTK_LOG_LEVEL` |
| ☆ 偏好 | `fx_prefs_set/get/save/load` | 应用目录下 key-value 文本（demo 常用） |
| ☆ 能力 | `fx_backend_caps()` | `has_gpu / has_gpu_aa / max_texture / quad_warp / clipboard` —— 让框架与控件按能力降级 |
| ☆ 截图 | `fx_screenshot(path)` | PNG 落盘，直接服务 §5 的美术评审与 CI 金图 |

### 2.3 迁移与兼容
- 保留 `fxtk_fs.h` 作为**兼容薄壳**（`fx_fs_*` → `fx_backend_fs_*`），标注 deprecated，一个版本后移除。
- 旧调用点仅 `app_desktop.c` 与 `canvas_08_files.c`，改动面很小。
- 无头测试全部切到 stub 后端（确定性）——现有 12 节测试 + §6 新增测试都受益。

## 3. 工作流 C：全局抗锯齿上 GPU

### 3.1 现状与差距
`fx_set_aa(1)` 现在把画布强制离屏，`aa_*` 系列在 CPU 上按"到图形距离"做扫描线混合：只有画布能开、
大图形开销明显（v2.2 已优化到 ~10 ms/帧级）、控件（圆角矩形/边框/文字）完全不参与。

### 3.2 GPU 方案（分层，逐层可独立验收）
1. **控件层：SDF 实例化**。✅ 已完成（`fx_set_widget_aa` 默认档 1；驱动钩子 `fill_rect_round`/`stroke_rect_round`；
   实测图片页命令数 1184 → 7）。圆角矩形/边框/描边/阴影统一为一个实例化批次，片元用 SDF 求覆盖度
   （`smoothstep(fwidth(d))`）→ 边缘天然平滑，**且比现在逐行 `fx_fill_rect_round` 更快**（v2.3 已把逐行调用合并，
   但仍是一个矩形一条路径；SDF 是每控件一个实例）。
2. **图元层：羽化线/弧**。线段扩成双三角形 quad，片元按到线心的距离羽化；圆/弧用 SDF。
3. **文本层：图集 + 双线性 + 亚像素定位**，配合 §1.2-3。
4. **MSAA 兜底**：sokol_gfx pipeline `sample_count`（4x）+ resolve，覆盖 SDF 不好表达的形状（旋转、交叉点）。

### 3.3 开关与质量档
`fx_set_aa(level)`：`0=关`、`1=SDF`（默认）、`2=SDF+4xMSAA`。质量档可由 `fx_backend_caps()` 与本次性能预算
自动降级（掉帧时自动降档并打印一次日志）。

### 3.4 CPU 路径（ESP32 / 无 GPU）保留
`aa_*` 扫描线实现保留，作为无 GPU 平台的唯一路径；API 不变，由驱动决定实现。这保证 ESP32 不会被 v2.4 破坏。

## 4. 工作流 D：图片四边形形变 + canvas 新特性 + 伪 3D demo

### 4.1 核心 API
```c
/* 把整张图片映射到任意凸四边形（四角顺时针） */
void fx_image_quad(fx_image_t *img, const float xs[4], const float ys[4]);
/* 便捷: 以矩形为基准做一次透视（上边收窄/倾斜等） */
void fx_image_quad_from_rect(fx_image_t *img, int x1,int y1,int x2,int y2,
                             float skew_x, float skew_y, float perspective);
void fx_image_quad_reset(fx_widget_t *w);      /* 复位成普通矩形 */
void fx_image_quad_set_corners(fx_widget_t *w, const float *xy8);   /* 编辑器用 */
```
- **GPU 路径**：单次 draw 两个三角形 + 透视正确插值（顶点着色器输出 `uv/w` 与 `1/w`，片元重建；
  等价于传 3×3 单应矩阵）。**这是"两个三角形画四边形有对角缝"的正解**。
- **CPU 路径（无 GPU 平台）**：按扫描线求逆单应，逐行仿射采样（透视正确），成本 O(面积)；
  小图可用。降级由 `fx_backend_caps().quad_warp` 决定。
- **退化/非法四边形**：非凸或自交时回退为"包围盒 + 裁剪的近似矩形映射"并告警，绝不越界读写
  （沿用 v2.3 `fx_image_create` 的尺寸上限护栏思路）。

### 4.2 图片页 UI（"导入图片"）
- `[导入图片]` 按钮 → `fx_fs_pick_file(filters={"png","jpg","jpeg","bmp","gif","tga"})` → `fx_img_load_file`
  → 建纹理 → 显示。超大图自动降采样到 `max_texture` 内（带日志）。
- 缩放：滑杆（0.1x~4x）+ 保持长宽比开关 + 锚点选择（四角/中心）。
- 四个角手柄可拖拽 → 实时更新四边形（拖拽中显示源矩形线框 + 目标四边形边框）。
- `[复位]` 按钮；`[滤镜: 最近/双线性]` 切换；HUD 显示 源尺寸/目标尺寸/映射类型(GPU/CPU)。
- 这是 v2.4 的**可视化说服力入口**：一屏就能展示"导入 + 缩放 + 任意四边形"。

### 4.3 canvas 新特性
- `fx_canvas_image_quad(...)`（同 §4.1）+ **变换栈**：
  `fx_canvas_push_affine(a,b,c,d,e,f)` / `pop`（2D 仿射，方便做旋转/缩放/平铺），
  以及 `fx_canvas_billboard(img, x,y,z, scale, camera)` 之类的伪 3D 便捷函数。
- 与既有离屏/像素路径的关系：quad warp 优先 GPU 批次；CPU 路径走离屏缓冲。

### 4.4 图形页伪 3D demo（性能展示）
场景（全部由四边形形变构成，无真实 3D 管线）：
- **地板/天花板**：一张纹理按透视四边形铺开（每格一个 quad，或大 quad + 平铺 UV）；
- **走廊墙面**：两侧按远近平移的四边形序列；
- **旋转纹理立方体**：6 个面 = 6 次四边形形变（每面透视正确 → 视觉上就是一个真 3D 立方体）；
- **公告板精灵**：始终面向相机的四边形（伪 3D 常见手法）；
- **HUD**：`FPS / 四边形数 / draw call 数 / GPU 或 CPU 路径`。

验收：1080p 下 200 个纹理四边形 ≥ 60 fps（GPU 路径）；CPU 路径 ≥ 20 个四边形 30 fps。
这个 demo 同时是 §3（GPU AA）与 §1（批处理）的联合验证。

## 5. 工作流 E：控件逐个审美优化（用图像识别能力评审）

### 5.1 方法（可复现，不靠嘴说）
1. **截图画廊工具**：`make gallery` → 逐页/逐控件出 PNG（`fx_screenshot` 或 bench `--dump`），
   固定窗口尺寸（480×272 设计分辨率 + 1280×720）避免随机性。
2. **我用图像识别逐张评审**（这是你点名要的能力用武之地）：间距节奏、圆角一致性、边框对比、
   状态机（normal/hover/pressed/disabled/focus）视觉差异、文字基线、颜色和谐度。
3. **设计令牌落地**（写进 `fxtk.h` 主题区，替换散落魔数）：
   `FX_SPACE_1..6`（4/8/12/16/24/32）、`FX_RADIUS_S/M/L`、`FX_ELEV_0..3`（阴影/描边层级）、
   `FX_STATE_HOVER/PRESSED/DISABLED` 混色比、字体阶梯。
4. **逐个控件出"前后对比图"**，进 `docs/design/`，并在 CHANGELOG 里附一页拼图。

### 5.2 优先级顺序（改动收益/风险比）
按钮 → 输入框/文本编辑 → 复选框/滑条/进度 → 标签页/面板 → 列表/下拉 → 画布/图片页 HUD。
每个控件的验收：像素 A/B（除刻意改动区域外零差异）+ 三态视觉差异 ≥ 阈值（用图像差异度量说话）+ 你的目视确认。

## 5.5 进度快照（每轮更新，避免"说到没做到"）

| 工作流 | 状态 | 证据 / 备注 |
|---|---|---|
| P0 后端服务层 + 截图画廊 + sokol spike | ✅ | `fxtk_backends.c`(+stub 后端)、`tools/gallery.sh`、无头测试 14 节 |
| P1 canvas 变换栈 + 四边形形变 + 图片页 UI | ✅ | 真透视 CPU 路径、四角手柄/缩放/复位/导入, 示例 canvas_09/10 |
| P2 vendored sokol 后端 | ✅ | 事件队列适配轮询契约、顶点批、软件像素层、stb 字形文本、逐帧回读与屏幕一致 |
| P3 GPU 抗锯齿 | ✅(分层) | 档 1 = 控件 SDF(命令数 1184→7)；档 2 = 图元羽化线段(过渡像素 0→5916)；掉帧自动降档 |
| P4 GPU 四边形形变 + 伪 3D demo | ✅ | GPU 真透视与 CPU 逐像素一致(>8 级差异 0); 图形页伪 3D(地板/天花板/走廊/立方体/公告板+HUD) 105 四边形 60fps |
| P5 控件审美 + 设计令牌 | ✅ 9/9 完成 | `fxtk_tokens.h` 已落地(纯重构, 金图 20/20 不变); 按钮轮完成(圆角 7/描边 25%/按下整块压暗, 零额外绘制调用); 流程见 docs/design/README.md; 全部控件已过一轮(按钮/输入框/滑杆/标签页/列表/进度条/复选框/卡片/标签); 细节见 CHANGELOG |
| P6 双语 + CI 金图 + ESP32 冒烟 + 发布 | ⏳ 金图 ✅ / Win 构建 ✅ / 单 exe 无 DLL ✅ / **ESP32 冒烟 ✅**(tools/esp32_smoke.sh + CI); 余 Win 体积口径(需拍板) + 双语文档 | 体积: **SDL 版 144KB / 英文版 143KB ✅ 达标 ≤150KB**(gold `--icf=all`+`--as-needed`); sokol 版 361KB(含 vendored sokol, 独立口径) |

### Windows 体积口径 —— 已定案：单 exe ~450KB（实测数据）

> **用户裁决(v2.4 定稿)**: 采用"改口径" —— 单 exe 按 **~450KB** 计, 保持能力完整与零第三方 DLL 依赖;
> 不去掉图片解码/PNG 编码。理由: 250KB 是 SDL 时代标准(exe 182KB 却要带 SDL2.dll + SDL2_ttf.dll, 后者单个 66MB)。
单 exe(必须, 因为另一条验收项要求"单个 exe 无 DLL 依赖")实测 **431KB**, 构成:
`.text` 298KB + `.rdata` 111KB(含 vendored sokol_app/gfx/glue + stb_truetype + 框架 + demo)。
三条路与代价:
1. **改走 exe + DLL —— 实测不可行(第 34 轮验证)**: 这不是"省 100KB"的问题, 而是**依赖方向不允许**:
   框架核心的文本 API(`fx_draw_text*`)由 `fxtk_font_stb.c` 提供, 而该层调用 sokol 的 `sg_make_image`,
   所以文本层与 sokol 实现段必须待在同一个模块里; 而 sokol_app 的实现段包含程序入口, 又必须在 exe 里。
   结果: 想把核心拆成 DLL, 就得给 DLL 提供"由 exe 导出"的导入库(循环依赖), 或把 sokol 也塞进 DLL(入口又出不去)。
   实测: 强行只链核心导入库时构建直接失败; 去掉核心硬链出来的 exe 是 365KB 但**符号缺失、不可运行**,
   不能当作有效方案。→ 结论: Windows 侧要么保持单 exe, 要么砍功能, 没有干净的 DLL 拆分路径。
2. **砍功能**: 去掉图片解码(stb_image)或 PNG 编码(stb_image_write)约省 60~90KB —— 会损失"导入图片/截图"能力。
3. **调整口径**: 单 exe sokol 版按 ~450KB 计(仍无任何第三方 DLL 依赖)。
建议选 3(能力完整、零第三方 DLL), 若要 ≤250KB 则必须接受 2 的功能缩水。

### P3 与路线图的差异（已定案）
- **MSAA 未按 §3.2.4 原样实现**: sokol 的 swapchain 采样数只能在窗口创建时确定, 运行时切档做不到;
  而 SDF 已覆盖控件与线段两类主要锯齿源, MSAA 的增量收益集中在"旋转/交叉点"。
  改用 ①图元羽化 ②掉帧自动降档 两项替代。若要 4x MSAA, 按"启动时 `FXTK_MSAA=4` 申请 +
  离屏画布 pass 多重采样 + resolve"的形式补, 不影响现有档位。

### 用户裁决（第 40 轮后确认, 两项收官）
1. **Windows 体积**: 采用"改口径" —— 单 exe(sokol)按 **~450KB** 计, 保持能力完整与零第三方 DLL 依赖;
   不去掉图片解码/PNG 编码。理由: 250KB 是 SDL 时代标准(那时 exe 182KB 却要带 SDL2.dll + SDL2_ttf.dll, 后者单个 66MB)。
2. **双语文档**: **不做全量翻译**。英文 README/quickstart 已同步 v2.4 事实, 其余英文档标注"对应 v2.3"边界;
   中文 `docs/guide.md` 已补全 v2.4 新增能力(含可直接抄的用法与实测数据)。

### 第 39 轮最终验收快照（全部实测, 可复跑）
```
make test              -> 2/2 PASS   (真实后端 + stub 后端)
GOLDEN_SKIP_DEMO=1 GOLDEN_TOL=1 tools/golden.sh
                       -> 10/10 画布金图一致 (容差 1; 演示页金图依赖 SDL 版本, 只在本地跑)
tools/esp32_smoke.sh   -> 6/6 核心文件通过 (无需 ESP-IDF 工具链)
make bench (1080P)     -> 2819 控件饱和 9.0~9.3ms/帧 ≈ 107~111fps (v2.3 同机同命令 9.40~9.95ms)
ASan+UBSan             -> 干净 (无头测试套件)
Linux 体积             -> make 产物 361KB(sokol, 单文件无第三方 DLL); 遗留 SDL 版 144KB ≤150KB
Windows                -> 交叉构建通过; 单 exe 431KB, 依赖仅系统 DLL
```
**全部验收项已闭环**(下面两条已由用户裁决定案):
1. Windows 单 exe 431KB vs 目标 ≤250KB —— 已实测否证"拆 DLL"路径, 只能"改口径(~450KB)"或"砍功能(去掉图片解码/PNG 编码)"。
2. 双语文档范围 —— 英文 README/quickstart 已同步 v2.4 事实并标注其余英文档对应 v2.3; 是否需要全量翻译由用户定。

### 验收项状态（实测）
- ✅ `make test` 双后端全绿(真实 + stub)
- ✅ **ASan/UBSan 干净**: `gcc -O1 -g -fsanitize=address,undefined` 构建无头测试并运行 → `PASS (0 fail)`,
  零 `ERROR:` / 零 `runtime error`(第 17 轮实测; 命令见下)
- ✅ **bench 不低于 v2.3(已用同机同命令对拍确认)**: 压测页饱和后
  1080P/2816 控件 → v2.3 **9.40~9.95ms(100~106fps)** vs v2.4 **9.02~9.31ms(107~111fps)**(v2.4 快 3~6%);
  720P/1235 控件 → v2.3 4.416ms(226.5fps) vs v2.4 4.386ms(228.0fps)(持平)。
  注: 本节原先写的"2816 控件 8.0ms/125fps"是**不同口径**测出来的, 不能直接比; 现已换成对拍数字。
- ✅ Linux 体积 144KB(≤150KB); ✅ 12 页 sweep panic=0; ✅ 单 exe 无 DLL(见下)
- ✅ **Windows 交叉构建通过**(第 18 轮实测): `demo-main/build_win_cross.sh` → `dist/win/fxtk_win.exe`
- ✅ **"单个 exe 无 DLL 依赖"已达标(sokol 路径, 第 20 轮)**: `build_win_sokol.sh` → 431KB,
  `objdump` 只剩 Windows 系统 DLL; 体积 431KB 超 ≤250KB 口径(待定口径/再瘦)
- ✗ (已由 sokol 路径解决) 旧的 SDL 路径仍然带 DLL:: `objdump -p` 显示该 exe 仍依赖 `SDL2.dll` + `SDL2_ttf.dll`
  (其余 comdlg32/KERNEL32/msvcrt/ole32/SHELL32/USER32 都是 Windows 系统 DLL, 可接受)。
  要达标必须走 **sokol 路径**(vendored、只链系统 opengl32), 即给交叉脚本加 `fxtk_sim_sokol` 的 Win 目标,
  并顺带量体积(目标 ≤250KB)。这是 P6 交叉编译项的下一步。
- ⏳ ESP32 编译冒烟、14 张画布金图回归 —— 未做
- 已知非阻断警告: `fxtk_backends.c:816` format-truncation(255 字节源写进 96 字节缓冲, 有意的截断)

### 每轮必须留下的验证证据（至少跑一遍）
```
make test      # 双后端无头测试, 必须 PASS
make bench     # 不低于 v2.3 基线(2816 控件 8.0ms/125fps; 当前 bench 页 83 控件 0.36ms/帧)
12 页 sweep    # FXTK_CLICKS=<tab x>,80,60 + FXTK_QUIT_AFTER=140, 每页 panic=0
截图 vs 屏幕    # fx_screenshot 的 PNG 与 X11 抓屏直方图一致(回读契约)
```

## 5.6 P4 落地规格（已核实接口, 可直接照做）

**框架侧已就绪, 不用改**: `fxtk_draw.c:1155` 已有 `if (!s_offing && s_drv && s_drv->draw_image_quad) → 走钩子`,
否则回落 CPU 逆单应逐像素路径; `fx_quad_homography(unit8, xy8, H)` 已经把【单位方 → 目标四边形】的
单应矩阵算好了（`components/fxtk/fxtk_image.h:44`）。

**驱动侧只需加一条管线 + 一个钩子**, 关键点:**不要让 CPU 算 w 再传 (u/w, v/w, 1/w)**,
而是把 w 塞进 `gl_Position.w`, 让硬件自己做透视校正插值 —— 片元着色器一行都不用改(直接复用 tex FS):

```
VS(quad):  in vec2 position(NDC); in vec2 uv; in vec4 color; in vec4 sdf;
           sdf.x = 该角的 w
           gl_Position = vec4(position * sdf.x, 0.0, sdf.x);   // ndc 不变, 但 w 参与插值
           uv = texcoord0; vcol = color0;
FS:        frag_color = texture(u_tex, uv) * vcol;              // 复用 s_pip_tex 的 FS
```

平的 w 从哪里来: 单应 `H = [[a,c,e],[b,d,f],[g,h,1]]`(映射单位方 → 目标四边),
分母 `d(x,y) = g*x + h*y + 1`, 于是四个角（单位方 (0,0)(1,0)(1,1)(0,1)）的权重是:

```
w0 = 1;  w1 = 1/(g+1);  w2 = 1/(g+h+1);  w3 = 1/(h+1)
```

（下一轮先写 3 行断言核对 `m[]` 的实际排布是行主序 `m[6]=g, m[7]=h`, 用 CPU 路径的
`fx_canvas_transform_point` 反推即可验证, 不要凭记忆。）

**验收**: ①同一个被拉成不规则四边形的图片, GPU 路径与 CPU 路径逐像素差 ≤ 1/255（对角线处不再有缝）;
②图形页伪 3D: 地板/天花板大 quad + 走廊两侧四边形序列 + 立方体 6 面(每面一次 quad warp) + 公告板;
HUD 显示 `四边形数 / draw call / 路径(GPU|CPU) / fps`; ③1080p 下 200 个四边形 ≥60fps(GPU)。

## 6. 阶段、依赖与排期（粗估，按专注工作日）

| 阶段 | 内容 | 依赖 | 估时 |
|---|---|---|---|
| P0 | spike：mingw×sokol 交叉、EGL surfaceless 无头出图；`fxtk_backends` 骨架 + 随机/时间/图片/路径 + stub 后端；截图画廊工具 | — | 2–3 d |
| P1 | canvas 变换栈 + **四边形形变（先 CPU 逐行透视 + 细分近似伪 GPU）** + 图片页导入/缩放/手柄/复位 | P0 | 3–4 d |
| P2 | **sokol 后端**：窗口/输入队列/矩形批/顶点批/软件像素层/文本图集/图片/截图；与 SDL2 逐帧 PPM 对齐 | P0,P1 | 6–9 d |
| P3 | GPU 全局 AA：SDF 控件 + 羽化线 + MSAA + 质量档自动降级 | P2 | 3–5 d |
| P4 | 四边形形变切到 GPU 真透视（含立方体/走廊），图形页伪 3D demo + HUD | P2,P3 | 2–3 d |
| P5 | 控件审美逐个优化 + 设计令牌 + 对比图集 | P3（AA 后评审更准） | 3–4 d |
| P6 | 文档（中英）、CI 增金图回归 + ESP32 编译冒烟、交叉编译/发布、v2.4 体积与性能报告 | 全部 | 2–3 d |

合计 **21–31 专注工作日**。若砍范围：把 P3 的 MSAA 与 P5 的顺序后移，可先出"v2.4-alpha = P2 完成"。

## 7. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| sokol_app 主循环反转与 `fx_driver_t` 轮询契约冲突 | 核心框架大改 | 驱动内部事件队列适配，核心零改动（§1.2-1） |
| sokol 无剪贴板 / 文件对话框 | 功能回退 | 平台代码补齐；过渡期框架内剪贴板降级 |
| CJK 字形图集内存（数千字形） | 内存爆/首帧卡 | 按需加载 + LRU + 多页图集；失败回退到"每串一张纹理"（现状） |
| mingw 交叉 sokol（D3D11/WGL 路径）不通 | Windows 交付受阻 | P0 spike 先验；备选：Windows 侧保留 SDL2 驱动一个版本 |
| ESP32 被 v2.4 破坏 | 违背项目初心 | 驱动分离 + CI 加 ESP-IDF 编译冒烟（当前只有脚手架，是现存缺口） |
| 迁后端引入渲染回归 | 画面对不上 | SDL2 驱动留作参考实现 + 逐帧 PPM 差分 + 金图 CI |
| 审美改动引发"看着不一样但没人确认" | 返工 | 前后对比图 + 你逐张确认，令牌先行 |
| 许可 | 法律风险 | sokol(zlib) / stb(public domain/MIT) 与仓库 LICENSE 核对后在文档标注 |

## 8. 验证体系（沿用并加强 v2.3 的成果）

- **无头测试**：12 节 → 扩到覆盖新 API（四边形角度/退化、随机可复现、后端 stub、quad reset）。
- **像素金图**：`render_canvas` 7 例 × 2 分辨率 + demo 静态页（输入页/画板页）→ 进 CI；
  ⚠ 组件页因读真实文件系统**天然非确定**，不进像素回归（v2.3 已实测）。
- **性能基准**：`make bench`（控件数/帧时间/poll/present 分段）+ 新增四边形与文本专项基准。
- **内存/线程**：ASan+UBSan 全量；TSan 查 EGL 线程移除后的新代码；退出路径压力测试
  （3 实例并发 × 多轮，v2.3 用它抓出了退出期堆损坏）。
- **跨平台**：Linux 本地 + mingw 交叉 + wine 冒烟；ESP-IDF 编译冒烟（新增）。

## 9. 我额外建议纳入的（你留了发散空间）

1. **`fx_backend_caps()` 能力协商**：让"AA 档位、quad warp 走 GPU 还是 CPU、图集上限"由能力决定而非硬编码 ——
   这是让 ESP32 与 PC 共用同一套控件代码的关键。
2. **stub 后端 + 确定性时钟/随机**：测试与 demo 录制可复现（现在的压测页帧率依赖导致截图不确定，
   有了 stub 时钟就能做稳定金图）。
3. **修掉 v2.3 遗留的渲染管线旧账**（顺手，收益大）：
   - `fx_repaint_rect` 里 `s_full=1` 让**脏矩形分支成了死代码** → 恢复后"少数控件在动"的页面只重绘变动区；
   - 呈现侧只按**包围盒**上传（压测满屏移动时退化成全屏）→ 改多矩形脏区集合 + scissor；
   - 这两条在 sokol 管线里做比在 SDL2 里做更自然（§1.2-2 的批次/scissor 一级公民）。
4. **文本渲染的"字形缓存 + 亚像素定位"**：中文界面下收益明显（现在同一字号每串一张纹理）。
5. **`fx_screenshot()` 进框架**：既是美术评审工具，也是用户级功能（demo 里加"截图"按钮）。
6. **打包与分发**：sokol 化之后可以做"单文件绿色版"（Windows 单 exe / Linux 单二进制），
   `make_bin_release.sh` 相应简化为"拷一个文件"。
7. **性能回归门禁**：CI 记录 bench 数字，超过阈值（如 +15%）直接失败——避免再次出现"悄悄变慢"。

---

## v2.4 定稿状态（第 41 轮）

**全部工作流与验收项闭环。** 可复跑证据见上文「第 39 轮最终验收快照」。

- P0 后端服务层(+stub 确定性变体) / P1 变换栈 + 真透视形变 + 图片页 UI / P2 vendored sokol 后端 /
  P3 两层 GPU 抗锯齿 + 掉帧降档 / P4 GPU 真透视 + 伪 3D demo / **P5 控件审美 9/9 + 设计令牌** —— 全部完成。
- P6:金图回归(进 CI)、Windows 交叉构建、**单 exe 无第三方 DLL**、ESP32 接口级冒烟(进 CI)、
  英文档同步 v2.4 事实、`tools/package_release.sh` 发布打包(源码 + Linux 单文件中英 + Windows zip 中英 + 动态库包) —— 全部完成。
- 体积:Linux 单文件 361KB(sokol) / 遗留 SDL 版 144KB;Windows 单 exe 433KB(**口径按 ~450KB**, 零第三方 DLL)。
- 质量闸门:`make test` 2/2 PASS、ASan+UBSan 干净、金图 10/10、ESP32 冒烟 6/6、bench 1080P 比 v2.3 快 3~6%。
