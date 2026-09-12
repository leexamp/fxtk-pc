# Changelog

## v2.4（开发中）

> 路线图见 `docs/ROADMAP_v2.4.md`。目标：sokol 自裁剪后端（替代实验性 SDL2）+ GPU 全局抗锯齿
> + 通用跨平台服务层 + 纹理四边形形变（真透视）+ 控件逐个审美打磨。

### 新增
- **通用后端服务层 `fxtk_backends.h/.c`**（P0）: 能力协商 `fx_backend_caps()` / 日志 / **PCG32 随机(同种子可复现)**
  / 单调时间 / 路径工具 / 文件与目录 / 图片解码(stb)与 PNG 编码 / 剪贴板 / 文件对话框 / 偏好持久化 / 系统信息。
  `-DFXTK_BACKEND_STUB` 提供确定性时钟 + 定种子随机 + **内存文件系统**，`make test` 同时跑真实与 stub 两份。
  设计原则：不引入新链接依赖（屏幕/DPI 由驱动注入）；PNG 编码经 `fx_file_write` 回调，真实 FS 与 stub 行为一致。
  `fxtk_fs.c` 已并入并删除，`fxtk_fs.h` 保留为 inline 兼容薄壳。
- **截图能力 + 截图画廊**（P0）: `fx_driver_t.read_pixels` 钩子 + `fx_screenshot(path)`（统一走 stb PNG 编码）。
  已接线 SDL 驱动与 render_canvas 假驱动（**无头 harness 也能出 PNG，CI 金图不需要显示器**）。
  `tools/gallery.sh` 一条命令生成 12 个演示页 + 9 个画布示例 × 2 分辨率。
- **四边形形变 `fx_draw_image_quad`**（P1）: 把整张图映射到任意凸四边形，**真透视**（单应矩阵求解 +
  逆变换逐像素双线性采样；"uv∈[0,1]"即凸四边形内判定）。附 `fx_draw_image_quad_persp` 参数化便捷版、
  `fx_quad_homography`/`fx_mat3_invert` 工具函数、退化四边形自动回退与告警。无 GPU(ESP32)与离屏画布走 CPU 路径，
  GPU 路径由驱动 `draw_image_quad` 钩子接管（P4）。新示例 `examples/canvas/canvas_09_quad.c`（矩形/梯形/强透视/旋转/斜切）。
- **canvas 变换栈 `fx_canvas_push_affine/pop_affine`**（P1）: 2D 仿射栈(深度 8, 每帧自动复位,
  后 push 的变换在外层)。push 后立即模式图元自动过变换: 像素/线段 → 端点; **矩形填充 → 实心四边形**
  (`fx_fill_quad`, 可旋转/斜切); **图片 → 透视四边形**(旋转/缩放/斜切一次到位)。附
  `fx_canvas_transform_point`/`fx_transform_depth`/`fx_transform_reset`。
  新示例 `examples/canvas/canvas_10_transform.c`(斜切墙面/旋转贴图/变换栈平铺地板/旋转网格线)。
- **vendored 依赖**（P0）: `third_party/sokol`（app/gfx/glue/time/log/fetch，pin 到 commit）+
  `components/fxtk/vendor/stb`（truetype/image/image_write/rect_pack）提交入库，CI 无需联网拉依赖。
  `tools/ppm2png.c` 提供 PPM→PNG 转换。

- **sokol PC 后端（P2，开发中）**：`fxtk_sokol_driver.c` + `main_sokol.c` + `fxtk_font_stb.c`
  （stb_truetype 文本层）+ `gpu_raymarch_stub.c`；`make fxtk_sim_sokol` 构建。
  - sokol_app 回调 → **事件环形队列** → 框架既有的轮询契约（`fx_driver_t` 零改动，ESP32/无头测试不受影响）
  - 绘制走**全批处理**：CPU 侧攒顶点+索引 → 每帧各上传一次 → 逐批 `sg_draw`（顶点带色，换色不打断批次）
  - 软件像素层（文字/抗锯齿/离屏画布）每帧一次整屏上传；截图在 pass 内、交换前回读
  - **持久离屏画布 + 整屏 blit**：与 SDL 驱动同构，保证脏矩形重绘框架的静态内容跨帧保留
  - 文本不再依赖 SDL_ttf/SDL_Renderer（stb_truetype 光栅 + LRU 纹理缓存 + 离屏像素路径）
  - 首个像素级对照：同一页(波形)SDL 215 色 vs sokol 209 色，各主色像素数差 < 1%
- **`fx_image_load` 迁到 backends/stb**：删除 `fxtk_image_sdl.c`，图片解码不再依赖 SDL_image
  （PNG/JPG/BMP/GIF/TGA 全支持，ESP32/无头环境同样可用）。

### 修复（sokol 后端，实测驱动）
- **颜色整体 R/B 互换（严重）**：`SG_VERTEXFORMAT_UBYTE4N` 是把**内存第 0 字节**喂给 vec4.x，
  小端下直接写 `0xAARRGGBB` 得到的是 `vec4(B,G,R,A)` → 片元 `frag_color = vcol` 让整屏红蓝互换
  （红按钮显示成蓝色）。顶点色现在与纹理路径统一走 `rgba_pack`（内存 R,G,B,A）。
  字体纹理层此前按 B,G,R 写（同一个错误结论的产物）也一并纠正。
- **"颜色已修好"的误判根因**：`fx_screenshot` 约定驱动 `read_pixels` 交出 `0xAARRGGBB`，
  而 sokol 驱动直接回传 `glReadPixels(GL_RGBA)` 的原始字——回读的 R/B 错误与渲染的 R/B 错误
  **互相抵消**，截出来的 PNG 看着正确、屏幕上却是错的，于是把"顶点直接写 0xAARRGGBB"当成了实测结论。
  现在回读显式搬一次字节，**截图与屏幕逐像素一致**（已用 X11 抓屏对照验证）。
- **图片页/画板页闪退**：所有图片共用一张 `s_img_tex`，而 sokol 校验"同一帧同一图片只能
  `sg_update_image` 一次"，一帧画第二张图就 `VALIDATE_UPDIMG_ONCE` → panic → abort。
  改为**图片池**（8 张轮转，暂存缓冲共用：GL 的 image update 是立即上传，安全）。
- **拖动/操作延迟高**：sokol_app 按系统速率逐个投递鼠标移动事件，而框架 `fx_poll` **每帧只消费一个**
  touch 事件（与 SDL 驱动同款语义），逐个入队会让界面用的坐标落后指针 100~200ms（实测排队
  max=216ms/avg=101ms、队列深度 15+）。改为**移动事件合并**（同状态时覆盖队尾，按下/抬起边沿原样入队），
  与 SDL 驱动"只保存最新鼠标坐标"等价 → 实测排队 max=0.0ms、队列深度 1、每帧 3 个事件也不积压。
- **3D 页用不了 GPU**：生成的 GLSL 把 `u_rect/u_time` 放在 `uniform` 块里，而本版 sokol 的 GL 后端
  用 `glGetUniformLocation` **逐个名字**定位成员——块成员拿不到 location → `sg_apply_uniforms` 直接
  `continue` 跳过，参数永远传不进去。改用松散 uniform，并把 3D 页接到 `fx_raymarch_available()` /
  `fx_draw_raymarch()`（满分辨率、不占 CPU 像素、不回读）。GPU 成为默认，"GPU: 开/关"按钮可切回 CPU 路径对比。
- **验证工具补齐**（两个后端对齐，便于 CI 与金图）：`FXTK_CLICK` / `FXTK_CLICKS`（多段点击，测跨页残留）、
  `FXTK_DRAG[,帧号]` / `FXTK_DRAG_LOOP`（持续拖动，模拟 125~1000Hz 鼠标）、`FXTK_SHOT[_AT]`、
  `FXTK_QUIT_AFTER` 现对 SDL 驱动同样可用；`FXTK_STAT=1` 输出**分段耗时**（输入排队/整帧/像素层/提交/
  图片数/文本纹理数与淘汰）——只报 fps 判断不出"手感延迟"。

### 新增（P3 GPU 抗锯齿, 第一层）
- **控件层 SDF 抗锯齿**（`fx_set_widget_aa(level)` 0/1/2, **默认 1**）: 圆角矩形填充/描边不再逐行拼,
  而是由驱动一次 draw 完成, 片元按到边界的**有符号距离**求覆盖度（`0.5 - d/fwidth(d)`）→ 边缘天然平滑。
  驱动钩子 `fill_rect_round` / `stroke_rect_round`；SDL/ESP32 无此钩子时自动回退原逐行路径（API 不变）。
  实测(图片页): 命令数 **1184 → 7**、顶点数 5162 → 212；压测页顶点 5996 → 3212, fps 不变。
  档位含义: `0=关`, `1=SDF`(默认), `2=1+图元羽化`(线段/圆/弧, 待做)。
  与画布 CPU AA（`fx_set_aa` 的 v2.2 语义, 默认仍关, 开销大）**解耦**：`fx_set_widget_aa()` 只动 GPU 层。
- **掉帧自动降档** `fx_aa_autodegrade()`: 驱动检测连续掉帧后调用, 降一档并打印一次日志（路线图 §3.3）。
- `FXTK_AA=0|1|2` 环境变量: 同页同帧 A/B 对比截图用（已用于目视核对圆角过渡像素 52→68）。

### 新增（P3 GPU 抗锯齿, 第二层）
- **图元层羽化线段**（档位 2）: 线段扩成四边形, 局部坐标沿线段轴向, 片元用**胶囊距离**羽化边缘 ——
  任意角度的斜线都不再有阶梯。与圆角矩形共用同一条 SDF 管线（`vsdf.z < 0` 表示胶囊, `|z|` 为半宽）。
  框架侧 `fx_draw_line_plain` 在档位 ≥2 且非离屏/非变换上下文时改走 `draw_line_aa` 钩子;
  无钩子（SDL/ESP32）自动回退原双三角硬边路径。实测波形页: 颜色数 234 → 575, 波形线周围过渡像素 0 → 5916, fps 仍 60。
- 档位含义定为: `0=关`, `1=SDF 控件层`(默认), `2=1+图元羽化`。`FXTK_AA=2` 可即时对比。

### 修复（P3 期间挖出的三个真 bug）
- **离屏 pass 的裁剪矩形被垂直镜像（严重, 影响所有紧裁剪绘制）**: sokol 的 render-target pass
  裁剪原点是左下, 而框架给的矩形是左上原点。传 `origin_top_left=true` 会让裁剪框上下翻转 ——
  表现一: 控件用自己的紧裁剪时被**整块裁掉**（SDF 圆角填充全部消失, 排查了半天）;
  表现二: 画布裁剪"看起来正常"其实只是画布矩形接近满屏、镜像后仍大面积重合的巧合。
  四种组合实测后定为 `sg_apply_scissor_rect(x, y, w, h, false)`（直接用屏幕坐标 y）。
- **顶点属性与结构体字段顺序必须一致**: 给顶点加 SDF 参数槽时, 先漏声明第 4 个属性、
  再是字段写在末尾（颜色读到偏移 32 处）—— 两次都表现为**整屏全黑**, 且没有任何报错。
  现在 `vtx_t{pos,uv,color,sdf}` 与 `layout.attrs[0..3]` 严格同序, 注释写明"别再改顺序"。
- **SDF 着色器不能复用带纹理绑定的 shader desc**: 复用会让 SDF 管线要求 view/sampler 绑定,
  未绑定直接 `VALIDATE_ABND_EXPECTED_VIEW_BINDING` panic。改用独立 desc。

### 新增（P5 审美迭代 6+7/9: 进度条 + 复选框）
- **进度条**: 轨道/填充/描边由硬直角改圆角(圆角按高度收敛, 短填充不会因圆角过大而变形);
- **复选框**: 方框由硬直角改圆角(令牌 S, 与输入框/列表同一套语言), 勾选标记形状不变; 留白走令牌;
- 两者**调用次数都不变** → 压测开销不变。
- 同源对比(金图 demo_p0 ↔ 重建后的 bench 渲染): 差异 100/921600 像素(0.01%), 放大可见绿色填充两端与轨道端头变圆。
- **过程坑(记下来免得再犯)**: `make` 只构建 `fxtk_sim`(sokol), **不会重建 `test/bench`** ——
  我第一次做同源对比时 bench 还是旧二进制, imgdiff 报"完全一致"差点让我以为改动没生效。
  做金图/同源对比前必须先 `make -B test/bench`(`tools/golden.sh` 内部自己会重建, 所以那边不受影响)。

### 新增（P5 审美迭代 5/9: 列表/下拉）### 新增（P5 审美迭代 5/9: 列表/下拉）
- 同类的"圆角不一致 + 顶格"问题: 列表外框原为硬直角, 选中行从 x=1 铺到 cw-2 **紧贴外框**。
- 改为: 外框圆角(令牌 S) + 选中/悬停行改成**内缩 2px 的圆角胶囊**(与标签页选中态同一套语言),
  文字左右留白改用 `FX_TOK_TEXT_PAD_X`; 滚动条配色也从字面量收进令牌
  (`FX_TOK_LIST_EDGE`/`LIST_HOVER`/`SCROLL_TRACK`/`SCROLL_THUMB`)。
- **调用次数不变**(填充/描边各 1 次, 只是换圆角版本); `make test` PASS; 已 `make golden-update` 更新基线。

### 新增（P5 审美迭代 4/9: 标签页）### 新增（P5 审美迭代 4/9: 标签页）
- 原先是"每格铺色 + 4 条立体线"的方格倒角: 与按钮/输入框/滑杆的圆角语言不统一, 一格要 5 次绘制。
- 改为: **选中 = 圆角胶囊高亮**(内缩 2px 留出间隔, 圆角走令牌), **未选中不铺色、不画立体线**
  (直接露标签条底色) —— 视觉更干净, 且 12 个标签从 ~60 次绘制降到 ~3 次(纯收益)。
- **过程中被"看图"这一步救了一次**: 第一版改完未选中标签的浅灰字压在浅色标签条上几乎读不出来(可用性倒退),
  目视对比后改成深灰(TEXT_DIM), 选中用正文色 —— 这正是 docs/design 流程里"必须看图"的价值。
- `make test` PASS; 刻意改观感 → 已 `make golden-update` 更新基线。

### 新增（P5 审美迭代 3/9: 滑杆）### 新增（P5 审美迭代 3/9: 滑杆）
- **修掉同类不一致**: 滑块一直是**圆角填充 + 直角描边** —— 圆角被描边的直角切掉, 看着像"方块贴了个圆角",
  和按钮/输入框的圆角语言也不统一。描边改用同半径的圆角矩形, 半径比例走新令牌 `FX_TOK_RADIUS_KNOB_DIV`。
- **零额外绘制调用**(仍是 1 填充 + 1 描边) → 压测开销不变。
- 实测: 滑块区域(100x50)内差异像素可见四角变化; `make test` PASS; 已 `make golden-update` 更新基线。

### 新增（P5 审美迭代 2/9: 输入框）### 新增（P5 审美迭代 2/9: 输入框）
- **修掉一处设计不一致**: 输入框一直是**硬直角**, 而按钮/滑杆都是圆角 —— 同屏看就是"两套设计"。
  改成圆角填充 + 圆角描边(圆角取 `FX_TOK_RADIUS_S`, 比按钮的 7 更"方"一点, 保持输入区的稳重感)。
- **零额外绘制调用**: 仍是 1 次填充 + 1 次描边(只是把 `fill_rect`/`draw_rect` 换成圆角版本),
  所以压测开销不变; 留白也从字面量 6 改用 `FX_TOK_TEXT_PAD_X` 令牌。
- 前后对比: 差异 2548/921600 像素(0.28%), 全部落在输入框四角; 放大目视可见四角变圆。
- 刻意改观感 → 已 `make golden-update` 更新基线; `make test` PASS。

### 新增（P5 审美迭代 1/9: 按钮）### 新增（P5 审美迭代 1/9: 按钮）
- 按 `docs/design/README.md` 的流程做的第一轮控件审美(改前留档 → 改后出图 → 逐图 imgdiff → 看图评审):
  - 圆角 4 → **7**(新增令牌 `FX_TOK_RADIUS_BTN`): 触摸目标边缘更柔和;
  - 描边加深到 25%(`FX_TOK_BTN_EDGE_MIX`): 按钮与背景分离度更好, 边缘更清晰;
  - 按下态整块压暗 12%(原先只压暗顶边), 触摸屏上"按没按到"一眼可辨。
- **刻意不增加绘制调用**(只改圆角与描边颜色) → 压测页开销不变(实测 1080P/2816 控件仍 9.2ms 级)。
- 前后对比: 差异 7848/921600 像素(0.85%), 全部落在按钮上; 放大目视可见圆角与描边改善。
- 这是**刻意改观感**的一轮, 已按流程执行 `make golden-update` 更新金图基线(20 张)。

### 变更（后端策略### 变更（后端策略: sokol 转为默认, SDL2 降为遗留）
- `make` / `make fxtk_sim` 现在构建 **sokol 版**（产物仍是 `./fxtk_sim`，361KB）；
  `make fxtk_sim_en` 也换成 sokol（361KB）；`fxtk_sim_sokol` 保留为别名，旧脚本/文档不用改。
- SDL 版移到 `fxtk_sim_sdl` / `fxtk_sim_sdl_en`，**明确标注为遗留**（仅对照/过渡，不再加新功能）。
- **为什么保留 SDL 代码**：`test/bench` 与 `tools/golden.sh` 依赖 `SDL_VIDEODRIVER=dummy` 做
  无头确定性渲染（CI 没有显示器，sokol 起不来 GL 上下文）—— 它现在的角色是"无头假驱动"，
  不是发布后端。发布产物一律 sokol，也是 Windows 单 exe 无 DLL 的前提。
- 实测: 默认 `make` 产物可运行出图(控件页 810 色)；`make test` PASS；金图回归 20/20 一致。

### 修复（粒子页常驻 40MB 静态内存）
- `app.c` 里 `static pt_t s_pt[2000000]` 常驻 **40MB `.bss`**，且首次进入要把 200 万粒子全部初始化 ——
  对一个以 ESP32 为目标的框架完全不可接受。
- 改为**按需分配**（`pt_reserve()` 扩容并只初始化新增区间；上限收敛到 40 万，滑杆 0~100 → 0~40 万）。
- 实测: sokol 版 `.bss` **52MB → 12.7MB**（余下是顶点/索引缓冲与控件池），粒子页渲染正常。

### 新增（P5 地基: 设计令牌）### 新增（P5 地基: 设计令牌）
- `components/fxtk/fxtk_tokens.h`: 把散落在 `fxtk.c` / `fxtk_widgets.c` 里的颜色与几何字面量
  收拢成一套语义化令牌(PRIMARY/SUCCESS/DANGER/TEXT_DIM/TRACK/KNOB/EDGE_DOWN/BORDER +
  圆角/轨道厚度/滑块宽度上下限)。**改这一处就能统一换肤/统一圆角**, 是后续控件审美优化的前提。
- 替换 18 处字面量(控件绘制 13 处 + 每类型默认色板 5 处);
- **本次是纯重构**: 令牌取值与改造前逐位相同 → `make test` PASS 且 **金图回归 20/20 逐像素一致(容差 0)**,
  即"没有顺手改坏画面"的机器证据。
- `docs/design/README.md`: 写入审美迭代的标准动作(改前留档 → 改后出图 → 逐图 imgdiff →
  纯重构要求金图不变 / 改观感才 make golden-update 并写清原因), 避免"我觉得更好看了"式空口评审。

### 优化（动态库再瘦身### 优化（动态库再瘦身: libfxtk.so 147.8 → 119.8 KB）
- ①`--exclude-libs,ALL` + gold 的 `--icf=all`（折叠等价函数）;
  ②版本脚本 `fxtk.exports` 只隐藏内部符号（`stbi_*`/`_sg_*`/`_sapp_*` 等）。
- **踩坑记录**: 版本脚本 `local: *` 不能配 `-fvisibility=hidden` —— sokol 实现段里的 `main` 会一起被隐藏,
  而版本脚本只能"降级"符号、无法把它再提升回来, exe 链接直接报 `main` 未定义;
  更坑的是给 **exe** 也加 `--version-script` 会把 `sokol_main` 降为局部符号 → 无人引用 →
  `--gc-sections` 把整个 demo 删掉, 产物变成 6KB 空壳(实测踩到)。现在库与 exe 用两套链接参数。
- 结果: exe 62.3 → **58.4 KB**, `libfxtk.so` **119.8 KB**(−19%), 截图与静态版**逐像素一致**(最大通道差 0)。

### 实测（性能: 压测页解除帧率限制后的上限）
无头 bench(SDL dummy 驱动, 无合成器/无 vsync), 压测页跑到控件饱和:

| 分辨率 | 饱和控件数 | ms/帧 | fps |
|---|---|---|---|
| 480x272 | 202 | 0.67 | 1478 |
| 1280x720 | 1235 | 4.34 | 230 |
| **1920x1080** | **2819** | **9.0~9.3** | **107~111** |

- 图形窗口下即使 `FXTK_NOVSYNC=1` 也仍是 60fps: **GNOME 合成器**把窗口统一限在 60Hz, 应用侧关 vsync 绕不过去
  —— 所以上面的数字才是框架的真实上限(poll 3.1ms + present 6.0ms)。
- 与路线图里记的 v2.3 基线(2816 控件 8.0ms/125fps)相比慢约 13%, **需进一步查**(present 占大头)。

### 新增（动态库打包 —— 多示例共享同一份框架）### 新增（动态库打包 —— 多示例共享同一份框架）
- `build_shared.sh` + `make shared`: 把框架拆成 **`libfxtk.so`**(核心 6 个 .c, 与后端无关) +
  **`libfxtk_sokol.so`**(sokol 平台层 + stb 文本; `SOKOL_*_IMPL` 只在这一个 TU 里) + app 层 exe。
  rpath 用 `$ORIGIN`, 整包拷到任何位置都能直接运行(实测拷到 /tmp/bundle 正常启动)。
- **体积**: exe **361KB → 62.3KB**(5.8 倍); 两个库 147.8KB + 326.4KB, 多个示例/多语言版共用同一份。
- **正确性**: 同一页截图与静态版**逐像素一致**(最大通道差 0), 证明拆分不影响渲染语义。

### 修复（图形页与 SDL 版不一致: 旋转贴图整块消失）
- **根因**: sokol 驱动的 `blit_img_rot` 自 P2 起一直是空实现(注释写着"走 quadrilateral"待办),
  而图形页正好用 `fx_draw_image_rot` 画两张旋转图片 —— 于是那两张图在 sokol 后端**完全不见**,
  与 SDL 版一眼可见的差别。
- **修法**: 用 P4 的 GPU 真透视四边形实现(旋转是仿射特例, 每角权重恒 1, 硬件做旋转+双线性);
  语义与 SDL 驱动严格对齐: 以 (cx,cy) 为中心、尺寸 dw x dh、角度取 **-ang 度**
  (SDL_RenderCopyEx 正角为顺时针, 框架传的是逆时针角)。
- **实测**: 修前后对比截图可见两张旋转图片恢复; 与 SDL 版的差异从"结构性缺失"降为动画相位差。

### 修复（压测页在 1280x720 段错误 / Windows 崩溃 —— 无限递归爆栈）
- **根因**: 上一轮修"滑块残影"时, 我把立即重绘改成了 `redraw_region()`(会遍历控件树),
  于是形成递归环: `draw_widget → redraw_region → redraw_widget_now → fx_set_value → 控件回调 → draw_widget …`,
  直接把线程栈压爆。压测页(2816 控件)在大分辨率下必崩 —— 这正是用户 Wine 下
  `virtual_setup_exception stack overflow` 以及金图跑批 `demo_p8 渲染失败` 的原因。
- **修法**: 加绘制重入计数 `s_draw_depth`(=在 draw_widget 外层计数), 绘制期间到达的"立即重绘"
  只标脏交给本帧统一重绘, 不再嵌套遍历。
- **实测**: `SDL_VIDEODRIVER=dummy ./test/bench 30 8 1000 1280 720 out.png` 修复前 3/3 段错误(exit 139),
  修复后 3/3 正常出图 + ASan 零报错; 同时滑块残影修复未回退(滑杆区纯白像素仍为 805)。

### 修复（Linux 无法导入图片）
- **两个真 bug**: ①文件过滤器是往 **96 字节小缓冲**里反复拼 `--file-filter`, 6 个扩展名必然截断 →
  zenity 收到畸形参数直接失败; ②zenity 失败后**没有回退 kdialog**(哪怕系统里装着)。
- **修法**: 过滤器改成**单一** `--file-filter='图片 | *.png *.jpg …'`(不再拼接); zenity 失败继续试 kdialog;
  标题里的引号/换行先净化; 新增 `FXTK_PICK_DEBUG=1` 打印实际命令行便于排查。
- **实测(用假 zenity/kdialog 脚本, 不弹窗)**: 过滤器串为 `*.png *.jpg *.jpeg *.bmp *.gif *.tga`;
  正常路径返回选中文件; 把 zenity 换成"失败"脚本后**正确回退到 kdialog** 并返回其选中文件。
- **新增无头导入通道** `FXTK_IMPORT=<路径>`: 不走系统对话框直接导入(服务器/CI 可用)。
  实测: 设该变量启动 → 图片页画布出现导入图内容(11137 色 / 9290 深色像素)。

### 新增（P6 金图回归）
- `tools/imgdiff.c`: 零依赖像素比对(vendored stb), 输出差异像素数/最大通道差 + 差异可视化 PNG。
- `tools/golden.sh` + `make golden` / `make golden-update`: 无头渲染 10 个画布示例(480x272, 纯软件驱动)
  与 10 个演示静态页(1280x720, SDL dummy)共 **20 张**金图, 与 `test/golden/` 逐像素比对;
  容差可选(`GOLDEN_TOL=2`)。**天然非确定的 p4(3D 页画 FPS 标签)与 p11(读真实文件系统+悬停)已排除**并在脚本里注明原因。
- 实测: 20/20 一致(容差 0); 自检: 拿两张不同的图比对 → 报 116548/130560 像素不同(89.27%), 证明检测有效。

### 修复（伪 3D"到后面就没了"）
- **场景周期性变空**: 伪 3D 原本让整个世界绕相机**连续旋转**, 转到背面时整条走廊都落在相机后面,
  所有四边形被投影丢弃 → 画面整个空掉(HUD 里四边形数从 105 掉到 7)。
  用户反馈"伪3D怎么到后面没了"就是这个。改为**有界摆动**(±22°, 周期约 14s), 走廊永远在相机前方。
- 实测: 帧 200 / 700 / 1500 / 2400 都有稳定几何(砖墙 9.2万~18.7万像素, 地板 9.2万~14.2万),
  修复前帧 1500 两类都是 0。

### 新增（P6：Windows 单 exe 无第三方 DLL）
- **驱动在 Windows 上也改用 OpenGL(GLCORE)**, 不再用 D3D11: ①D3D11 要另写 HLSL, 而全部着色器都是
  GLSL 330; ②D3D11 后端下 `glReadPixels` 截图失效(金图回归没依据)。Windows 自带 opengl32.dll, 正好。
- 新增 `demo-main/build_win_sokol.sh`: 不链 SDL, 只链系统库 + `-static -static-libgcc`
  (否则 raymarch 的线程会引出 `libwinpthread-1.dll`, 又变成要带 DLL)。
- **实测达标**: `dist/win_sokol/fxtk_sokol.exe` 的 `objdump -p` 只剩 Windows 系统 DLL
  (comdlg32/GDI32/KERNEL32/msvcrt/ole32/SHELL32/USER32), **无 SDL2.dll / SDL2_ttf.dll / libwinpthread**。
- 体积 431KB: 超过路线图 ≤250KB 的口径(那是 SDL 时代的 demo 目标; sokol 后端自带 sokol_app+gfx+glue
  与 stb_truetype)。**关键项"单 exe 无 DLL"已达标**, 体积项待定口径或后续再瘦。

### 新增（P4 第二部分：图形页伪 3D demo）
- 图形页新增第 4 个模式「伪3D(四边形形变)」: 场景**全部由 `fx_draw_image_quad` 拼出, 没有 3D 管线** ——
  地板/天花板(每格一个四边形, 近大远小)、走廊两侧砖墙(按 z 分段的四边形序列)、
  旋转纹理立方体(6 个面各一次形变 + 画家算法按深度排序)、公告板精灵(屏幕空间四边形, 永远正对相机)。
- HUD 显示 `四边形数 · fps · 形变路径(GPU/CPU)`; 新增 `fx_quad_warp_gpu()` 供 HUD 判断。
- 驱动新增**一帧内上传去重缓存**: 同一张源图(指针相同)只上传一次。没有它, 上百个四边形会耗尽
  图片池(第 8 张之后的上传被丢弃)—— 表现为"画面全空, 但 HUD 显示画了 94 个四边形"(实测踩到)。
- 实测: 105 个四边形 / 1280x720 下 fps 60(GPU 形变路径)。

### 修复（"脏区未清除"的真正根因 —— 拖滑杆留一串滑块残影）
- **`redraw_widget_now()` 立即重绘不擦背景（严重）**: 按钮/滑杆/进度条/复选框的"立即重绘"分支
  只设了裁剪就把控件画上去, 完全不擦旧像素。于是拖动滑杆时, 每个中间位置都留下一个白色滑块,
  拖一次就攒出一排残影（用户截图里滑杆区域那一片等间距条纹就是这个; 文本光标、进度条同理）。
- 修复方式: 立即重绘改为与脏区路径 **同语义** —— 调用 `redraw_region()`（先用窗口背景铺满该矩形,
  再重画所有与之相交的控件, 容器自己会铺自己的底色), 所以放在彩色卡片上的控件也不会被凿出洞。
- 实测证据（滑杆区域内纯白像素, 单个滑块约 1000）: 拖动前 **6094 → 修复后 805** ✓;
  目视对比: 修复前拖动路径上 8 个滑块残影 → 修复后仅当前滑块一个。
- 复现方法（已固化, 供回归）: `FXTK_CLICKS="80,80,60" FXTK_DRAG_LOOP="390,604,790,604,8,1,120"`
  + `FXTK_SHOT`，然后数滑杆矩形内的纯白像素数。

### 新增（P4 第一部分：GPU 真透视四边形）
- **驱动实现 `draw_image_quad` 钩子**（框架侧早已就绪, 缺的一直是驱动）: 新增 `quadwarp` 管线 ——
  顶点着色器把**每角透视权重 d_i 放进 `gl_Position.w`**, 硬件据此做透视校正插值 uv;
  片元着色器与普通贴图完全相同(复用 `fs_tex_src`)。这是"两个三角形各做仿射 → 对角缝"的正解:
  单次 draw 即透视正确。权重由 `fx_quad_corner_weights()` 从单应算出(上一轮已验证)。
  退化/自交四边形回退成包围盒直绘(与框架 CPU 路径的回退语义一致)。
- 图片上传逻辑抽成 `img_upload()`, 与 `drv_blit_img` 共用图片池; 新增 `pip==4` 命令类型。
- **修框架一处契约错**: GPU 钩子拿到的是**屏幕坐标系**, 而框架只给裁剪矩形加了画布原点偏移
  (`s_ox/s_oy`), 四角坐标原样传出 → 整幅图偏移一个画布原点(实测偏 (41,152))。
  现在四角同样加上偏移; CPU 路径不受影响(它在离屏缓冲里按局部坐标画完再整体 blit)。
- **验收实测**: 同一张图片、同一个被拖歪的四边形, GPU 与 CPU 两路径逐像素比较 ——
  差异 >8 级的像素 **0 个**(0.000%), 其余 28.8% 差异均在 8 级以内(两条路径双线性采样相位的正常差别),
  目视两图完全一致。A/B 开关 `FXTK_NO_QUADGPU=1` 可强制回 CPU 路径做对照。

### 工程链
- 构建清单同步新增 `fxtk_backends.c`（Makefile / build*.sh / CI / ESP32 CMakeLists）；Win32 侧补 `-lcomdlg32`。
- `tools/spike/README.md` 记录 sokol 迁移的 7 个坑位（EGL 上下文类型、GLSL 310 es、uniform block 名、
  surfaceless 无默认帧缓冲、stdout 缓冲、mingw 链接等）。

### 测试
- 无头测试扩至 14 节：新增 [13] 后端服务层（随机可复现/边界、路径、文件往返+追加、PNG 往返、
  偏好持久化、能力/时间单调、截图优雅失败）与 [14] 四边形形变（四角精确映射、逆单应回代、退化拒绝）。
- 真实后端与 stub 后端**都**跑同一套测试。

## v2.3

> 本版次随发布做了一次全面审查, 覆盖工程链(构建/CI)与正确性(渲染/核心/平台)两部分。
> 所有修复均经 12 节无头测试、28 个目标干净构建、7 个画布示例修复前后像素级(PPM)回归、
> ASan+UBSan 全量清洗与真实 demo 运行验证。

### 新增
- **渲染吞吐基准 `demo-main/test/bench.c`（`make bench`）**：dummy 驱动无 vsync 测纯吞吐，可注入窗口 resize
  模拟大屏、切换任意页、输出「控件数 / ms-per-frame / poll / present」分段耗时，并支持回读渲染目标存 PPM
  做 A/B 像素回归。压测页（页 8）在 1920×1080 下自动增长到 2816 控件，是本次性能问题的复现入口。
- **可选控件编译（减小体积）**：`fxtk_internal.h` 新增 `FXTK_WIDGET_*` 配置宏（默认全 1）。用 `-DFXTK_WIDGET_XXX=0` 编译时裁掉对应控件的**绘制/创建实现**，减小二进制（为 ESP32 等体积受限平台）。可裁剪：BUTTON/LABEL/GRID/CANVAS/SLIDER/PROGRESS/CHECKBOX/PANEL/TAB/IMAGE/TEXTEDIT。
  - 实现：`fxtk_widgets.c` 各控件绘函数、`fxtk_extra.c` 的列表/下拉用 `#if FXTK_WIDGET_XXX` 包裹；`fxtk.c` 的 `draw_widget`/`redraw_widget_now`/`draw_canvas_only` 对应 case 用 `#if` 包裹。
  - 验证：默认全开行为不变；裁掉按钮+复选框 170KB→142KB，且编译/链接通过、无 undefined；甚至 `FXTK_WIDGET_CANVAS=0` 也能编译；关闭 list+drop 后无头测试仍通过。
- **标签页侧边栏方位 `sidebar(side)`**：标签条可放上(`FX_TAB_TOP`)/左(`FX_TAB_LEFT`)/右(`FX_TAB_RIGHT`)/下(`FX_TAB_BOTTOM`)四边。英文标签较长时常用左右侧（`FX_TAB_SIDE=88` 宽），顶部/底部用 `FX_TAB_H=24` 高；`fx_draw_tab`/命中/页码计算均按方位适配。用于 v2.3 国际化（英文标签更长）。
- **国际化（i18n）**：新增英文文档 `docs_en/`（README_en/quickstart_en/api_en/guide_en）与英文 demo `demo-main/app_en.c`（左侧边栏放长英文标签）；新增 Makefile 目标 `make fxtk_sim_en` 构建英文 demo；中文保持 `docs/`+`app.c`。英文文件一律 `_en` 后缀。
- **自动裁剪 `tools/autotrim.sh`**：扫描源文件用到的控件工厂宏，自动输出"未使用控件"的 `-DFXTK_WIDGET_XXX=0` 开关，无需手动跟踪（示例 ex01 100KB→88KB）。
- **内存微优化**：文本贴图缓存 `TEXT_CACHE_SIZE` 256→64（省缓存内存，命中不足自动重建）；`FXTK_MAXW` 真正生效（限制窗口上限，防大窗口撑爆窗口尺寸表面/GL 目标）。注：demo 的 ~180MB 驻留基准为 SDL2+GLES2 驱动基线（非泄漏，近 3000 控件压测稳 60 帧）。
- **抗锯齿性能**：`aa_*` 全部改为扫描线（见 v2.2 修复）。
- **发布产物体积优化**：默认构建档改为 `-Os + LTO + gc-sections + 去 unwind/ident`——
  Linux demo 154KB→101KB（**-35%**），Windows exe 801KB→182KB（**-77%**，旧交叉脚本连 `-s` 都没加）。
  需要 -O2 调试时 `make CFLAGS="-O2 -g" LDFLAGS=` 即可。
  ⚠ mingw 坑实录: `-fdata-sections` 与 LTO 同用会让 PE 的 `.data` 实体化 ~44.6MB 零填充
  （ELF 无此问题），Windows 侧必须剔除——`build_win_cross.sh` 注释里已立牌。
- **跨平台文件 API `fxtk_fs.h/.c`**：`fx_fs_pick_dir(out,cap)` 系统对话框选文件夹（Win32 `SHBrowseForFolderW` / Linux `zenity`）；`fx_fs_list(dir,out,max)` 列目录内容（Win32 `FindFirstFileW` / POSIX `opendir+stat`），返回 name/is_dir/size/date。双平台编译通过；`build_win_cross.sh` 加 `-lshell32 -lole32`。
- **demo 组件页（文件浏览器 + 颜色选择器 + 可拖动组件）**：
  - 左列名字列表编辑器：`fx_grid_map(dense())` 网格排列输入框+"+"，文本框添加+删除，字号滑杆（12~60）缩放"示例缩放文本"。
  - 右列：可拖动组件区（3 盒可拖、clamp 画布内、带边框）；文件夹浏览器（选中文件夹预览 → 表格呈现 名称/大小/日期，竖分隔线、行灰底、长名省略号、**可拖动/滚动的平滑滚动条**（悬停变宽变蓝）、**悬停提示**（跟鼠标显示完整信息）、点击行选中+悬停边框）；`选择文件夹预览` 按钮（跨平台 `fx_fs_pick_dir`）。
- **框架层修复**：tab 子控件裁剪到 tab 自身矩形（防画到 tab 外）；`draw_canvas_only` 的 TAB case 同样裁剪；字号缓存淘汰时**不关正在用的 `g_font`**（防 use-after-free 段错误）；`fx_list_clear`/`fx_textedit_text`/`fx_set_fgcolor_w` 新增。
- **控件页颜色选择器**：两套独立 RGB（背景改按钮底色 / 文字改按钮字色），文本框无 "n/3" 计数，实时更新示例按钮+颜色显示区。
  - 注：关掉的控件仍可创建（枚举/创建宏不变），只是不绘制；请只裁确实不用且跑通的控件。

### 修复 — 工程链
- **恢复 `demo-main/Makefile` 统一构建入口**（此前宣布过它, 但仓库中并不存在, 导致 README 快速开始与 CI 全断）:
  `make` / `make fxtk_sim_en` / `make test` / `make exXX_*` / `make canvas_*` / `make clean`。
  `test` 目标显式 `.PHONY` + 真实规则, 根除"同名 test/ 目录被 make 判为最新"的假成功
  （此前 `make test` exit 0 却一行测试都没编译运行）。
- **四处源清单补上 `fxtk_fs.c`**: `build.sh` / `build_ex.sh` / `build_win.sh` / CI warnings 步骤此前漏编,
  `./build.sh` 链接必败（undefined reference to `fx_fs_pick_dir`/`fx_fs_list`）。
  现以 Makefile 的 `CORE` 清单为唯一事实来源。
- **`build_win.sh` 示例路径修正**: `examples/$EX.c` → `../examples/$EX.c`（demo-main 下不存在 examples/ 子目录）。
- **CI**: warnings 步骤编译失败时不再 `exit 0` 假装成功, 并移除形同虚设的 <64 "门禁"; windows-cross job
  在交叉编译前先执行 `setup_win_cross.sh` 下载 vendored SDL2 开发包（此前 third_party/ 不在仓库, job 必红）。
- **`tools/autotrim.sh`**: 匹配正则补 `_p` 变体（`fx_list_new_p` 等）。旧正则对只用 `_p` 变体的源码会误判
  "未用 LIST"并产出让链接失败的 `-DFXTK_WIDGET_LIST=0`——对自家 demo 即复现。
- **Windows 交叉编译依赖自举**：`build_win_cross.sh` 检测到缺编译器/vendored SDL 时自动调用
  `setup_win_cross.sh`（不再直接失败）；setup 改为幂等（已就绪秒退），工具链按 apt/pacman/dnf 自动安装，
  SDL2/SDL2_ttf/SDL2_image mingw 开发包下载带重试与解压校验（URL 已逐一验证）。
  实测：Linux 上产出 PE32+ exe + 三个 DLL，wine 冒烟进入主循环。
- **ESP32 `CMakeLists.txt`**: 补齐缺失的 `fxtk_effects.c` / `fxtk_extra.c` / `fxtk_fs.c`（旧清单组件链接必败）。
- `test/headless_test.c` 内过时的手动构建注释改为与 Makefile 一致。

### 修复 — 正确性
- **P0 渲染**: `fx_image_create` 对 >32767 的宽高返回 NULL（旧: int16 截断为负 → `fx_draw_image_ex` 越界读 SEGV, ASan 实证）。
- **渲染**: GPU 快路径（draw_line/fill_tri）补上裁剪 —— 旧"去clip保批"使被 tab/滚动/画布上下文裁剪的线条溢出到邻居控件; 行缓冲滞留像素在离屏 blit/文字 blit/旋转 blit 前先刷出（修 z-order 违例）; 负坐标画布离屏 blit 与屏幕求交（旧 `(uint16_t)` 回绕整块丢失）; `aa_arc` 负角度区间（如 -90..90）归一化+模长比较（旧跨 0 段整段丢失）, 弧端点边界像素不再丢失; `fx_fill_rect_gradient` 末行到达 c2; `fx_set_clip` 饱和防 int16 回绕; 行缓冲扩容失败不再把未写入像素当数据刷出。
- **核心**: `te_grow` 返回成败, OOM 时 `te_insert` 放弃插入（旧: 粘贴+OOM 堆越界写）; `hit_test` SCROLL 分支检查容器/子件可见性并拒绝视口外触点（旧: 点击滚动框外任意位置按 +scroll_y 命中看不见的子件）; grid 引用布局期重解析（旧: 先子后父/名字打错 → 子件 (0,0,0,0) 静默）; `percent()` 钳到 [-1,1]（旧: "100,100" int16 回绕把控件甩出屏幕）; 只读文本框禁止 Ctrl+L 清空 / Ctrl+X 剪切; `fx_init`/`unlink_free` 清理全部悬垂状态（ctx 弹层/滚轮目标/滚动状态池/extra 槽位）; scroll 状态池满退化为无缓动直滚（旧: 静默别名到 [0] 两控件互踩）; 列表弹层条目为别名指针, `fx_list_clear` 先关弹层再释放（修 UAF）, 属主删除后弹层安全停放; title/tab 标签截断按 UTF-8 边界回退; `te_next_off/te_prev_off` 判空。
- **控件**: 滑条 value=0 不再画 1px 假填充（filled-1 反向矩形）。
- **渲染性能（2816 控件压测，1920×1080，gprof 定位）**：**13.25ms → 8.0ms/帧（75 → 125 fps，-40%）**，
  框架侧 poll **6.82 → 2.85ms（-58%）**：
  - **矩形批处理**：`fx_fill_rect_round` 原来**逐行**调 `fx_draw_hline`（一个 20×16 圆角按钮 16 次驱动调用，
    该函数独占 40% 自身耗时），裁剪量恒为 0 的中间带 `[y1+r, y2-r]` 现合并成一次填充；SDL 驱动原来
    「每矩形一次 SetRenderDrawColor + RenderFillRect」（压测下 **29,113 次/帧**），现连续同色矩形攒批走
    一次 `SDL_RenderFillRects`，颜色变化 / 顶点批 / 纹理混合 / 呈现前自动冲刷，绘制顺序完全保持。
  - **文本宽度缓存**：`fx_text_width`/`fxtk_text_width_size` 原来每次都调 `TTF_SizeUTF8`，而按钮/标签每帧
    都要量文字宽度（压测下 **1276 次/帧**）——该耗时在 libSDL2_ttf 内部，gprof 看不见却是大头。现用开放
    寻址哈希缓存（字体指针 + 字符串），字符集满 75% 整体重建，字体销毁时同步失效。
  - **文本贴图缓存查找**：先比 32bit 键哈希再比字符串（旧：每次 lookup 一次 snprintf + 最多 64 次 strcmp）。
  - **控件计数 O(1)**：`fxtk_widget_count` 旧实现每次扫 4096 项（约 40μs/次，demo 每帧调用）→ 改存活计数；
    分配改为游标查找（旧实现每次从 0 扫描，压测页每帧建控件时是 O(n²)）。
  - `fx_text_width_n` 短串改用栈缓冲（逐字符测量路径免 malloc）。
  - 正确性：14 张画布渲染 + demo 静态页（输入页 / 画板页）回读像素**逐字节一致**。
- **字体**: `fxtk_put_px` 声明修正为 uint32_t（旧与定义类型冲突, LTO 实证 UB）; `fx_text_width_n`/`fx_draw_text_c_n` OOM 判空。
- **平台**: SDL 驱动 `push_pixels` 在 set_window 之前收到像素不再除零; raymarch 线程池创建失败按实际数降级（旧: 任一 pthread_create/barrier 失败 → 主线程永久死锁在 barrier）; gpu_raymarch 共享状态改锁内读取, 新增 `gpu_raymarch_shutdown`（atexit 注册）; `fx_init` 重置时同步清理 extra 模块静态池; fxtk_fs Win32 路径转换检查+限长拼接+64 位文件大小。

### 测试
- 无头测试扩至 12 节: 原有 8 节 + 新增回归 [9] 超大图像拒绝 / [10] SCROLL 隐藏子件不可点（正反例）/ [11] grid 晚绑定 / [12] percent 钳制。
- 验证矩阵: 28 目标干净构建 0 error; 7 画布示例 PPM 修复前后对比（6 个完全一致, canvas_07_aa 仅弧终点 3 像素为改善性差异）; ASan+UBSan 下无头测试与全部画布示例 CLEAN; 真实 demo（SDL dummy）运行干净。

### 说明
- **退出时堆损坏（已修，根因实录）**：1920×1080 压测长跑约 25–33% 概率在**退出时**报
  `corrupted size vs. prev_size in fastbins`（3 实例并发可稳定复现 **8/24**）。
  根因：`gpu_raymarch_shutdown` 只置 `g_quit` 就返回，而该 atexit 处理器在 `SDL_Quit` **之前**运行（LIFO），
  于是主线程拆 SDL/GL 的同时 GPU 线程仍在驱动内部使用 EGL，破坏了驱动的小块堆。损坏发生在**未插桩的
  驱动库内部**，所以 ASan（3000 帧）与 TSan 均不可见，gdb 拖慢时序也会掩盖它。
  修法：shutdown 改为**限时 join**（`pthread_timedjoin_np`，上限 2s；超时照常退出，不会把关窗口变成挂死）。
  修复后同条件并发压力 **0/12 崩溃**（对照 8/24）。
- 实测澄清: 裁剪 BUTTON+CHECKBOX 的体积收益在 .o 级约 2.3KB, 链接产物大小不变;
  此前所记 "170KB→142KB" 无法复现, 待用统一 Makefile 重新标定。

## v2.2

### 新增
- **画布学习教程** `examples/canvas/`：canvas_01_primitives / 02_immediate / 03_offscreen / 04_custom_widget / 05_image / 06_interact / 07_aa，从图元到交互到抗锯齿逐级讲解，带详尽注释。
  - 所有示例把内容写成 `cw`/`ch` 的**比例坐标**，窗口任意缩放（大/小）内容都跟随画布、不挤角、不越界；并用无头渲染验证了 480x272 / 800x480 / 200x112 都不崩坏。
- **无头渲染工具** `demo-main/test/render_canvas.c`：用假驱动把任意 canvas 示例渲染成 PPM，无需 SDL/窗口/字体即可在不同窗口尺寸下查看/回归（CI 已加入渲染冒烟测试）。
- **便捷画布 API**：
  - `fx_canvas_size(w, &cw, &ch)` — 取画布本地宽高，替代重复的 `fx_widget_rect` + 宽高样板。
  - `fx_canvas_clear(w, color)` — 一键把画布清成指定颜色，替代 `fx_set_color` + `fx_fill_rect` 样板。
- **抗锯齿（平滑渲染，默认关闭/opt-in）**：`fx_set_aa(1)` 开启（默认关，避免把一切画布改成离屏/CPU 渲染而影响按直接/GPU 绘制设计的页面，如滚动页）——`fx_draw_line`/`fx_draw_circle`/`fx_fill_circle`/`fx_draw_rect`/`fx_fill_rect_round`/`fx_draw_arc`/`fx_draw_ellipse`/`fx_fill_ellipse` 等按到图形的距离/子像素覆盖做边缘混合，边缘平滑。调 `fx_set_aa(1)` 后**画布自动离屏**以支持混合（无需手动 `fx_canvas_set_buf`），示例 `canvas_07_aa`。
  - **抗锯齿性能**：`aa_circle`/`aa_fill_circle`/`aa_ellipse`/`aa_fill_ellipse`/`aa_arc`/`aa_fill_rect_round` 由"整框逐像素 sqrt"(O(r²))/SDF 逐像素改为**扫描线**（每行 1 次 sqrt/算角内缩，只扫边缘带/边缘列 + 填充内部），约 **60× 更少 sqrt**，大圆实测 ~9.7ms、含圆角矩形 ~10.7ms/帧且边缘依旧平滑；并移除不再使用的 `sd_round_box`。
- **渐变填充**：`fx_fill_rect_gradient(x1,y1,x2,y2,c1,c2,vertical)` 在矩形内做 `c1→c2` 线性渐变（vertical=1 上下，0 左右），用于按钮/进度/背景等。
- **离屏缓冲推广到任意画布**：`fx_canvas_enable_buf` 去掉按名字（rt_cv/pt_cv/…）的性能锁，并加 4M 像素内存护栏；任何画布都能 `fx_canvas_set_buf(w, 1)` 使用离屏缓冲。
- **canvas 性能**：
  - `fx_fill_rect` 离屏路径加**逐行直写 offbuf 快刷**（不再逐像素经 `fxtk_put_px`/边界检查），大块填充（棋盘/背景）显著提速。
  - 行缓冲改为**按连续段刷新**（见"圆/圆弧修复"），既修伪影也避免多余像素上传。
- **命名颜色常量**：`FX_WINDOW_BG` / `FX_WINDOW_DARK` / `FX_UI_FG` / `FX_BTN_BLUE` / `FX_OK_GREEN` / `FX_RED_ACCENT` / `FX_PURPLE` / `FX_CANVAS_DARK`，统一各处灰色（消除 240/245 不一致）。
- **CI**：`.github/workflows/ci.yml` — Linux 构建 + 无头测试 + 示例/画布示例 + 警告报告，Windows 交叉编译。

### 修复
- **头文件 include guard 尾部落空**：`fxtk.h` / `fxtk_desktop.h` 的 `#endif` 移到文件末尾，所有声明回到 guard 与 `extern "C"` 内（修复 C++ 链接/重复包含隐患）。
- **数字宏枚举归并**：`FX_W_IMAGE`/`FX_W_TEXTEDIT`/`FX_W_SCROLL`/`FX_A_IMAGE`/`FX_A_MAXLEN` 并入 `fxtk.h` 主枚举，消除 `-Wswitch` 警告。
- **`build_win.sh`**：删除对不存在脚本 `tools/gen_gpu_stub.py` 的依赖，补上缺失的 `fxtk_extra.c`。
- **`strdup` 隐式声明**：`fxtk_extra.c` / `fxtk_font.c` 增加 `_POSIX_C_SOURCE`，严格 `-std` 下可编译。
- **圆/圆弧/椭圆渲染伪影（蝴蝶结）**：行缓冲之前按 `x0..x1` 整段 `push`，会把其它行的旧像素一起带上，导致画布上的圆/圆弧/椭圆出现“实心/蝴蝶结”。改为**按连续段刷新**（断点即刷），圆/圆弧恢复为干净描边。（用 `demo-main/test/render_canvas.c` 无头渲染复现并验证。）
- **画布随窗口缩放**：canvas 示例内容全部改为 `cw`/`ch` 比例坐标，窗口任意缩放都跟随、不挤角、不空白；无头渲染验证 480/800/200 三尺寸不崩坏。
- **窗口放大后 canvas 空白 / 控件消失（根因修复）**：`fxtk_sdl_driver.c` 的 `sdl_apply_size` 在 resize 时先把 `fb_w/fb_h` 设成新尺寸、**再**释放 `fb_rgba`，导致 `fb_ensure` 判定"尺寸未变"而**永不重新分配** `fb_rgba`；随后 `sdl_push_pixels` 因 `fb_rgba==NULL` 直接丢弃所有**软件渲染像素**（离屏 blit、圆/圆弧/描边/文本等），于是放大后 canvas 空白、部分控件消失。改为释放后置 `fb_w=fb_h=0`，让 `fb_ensure` 按新尺寸重分配。该修复覆盖 canvas03 的"放大空白"与 canvas04 的"转盘/描边消失"（软件像素在 resize 后不再被丢弃）。
- **离屏缓冲（+ 缩放限制说明）**：离屏画布在窗口**放大**时受驱动器/合成路径限制可能出空白（demo 的 3D 页也为此主动关离屏）。已在 canvas_03 标注，离屏建议用于固定尺寸/静态内容；`fxtk_draw.c` 的 `fx_canvas_enable_buf` 推广到任意画布并加 4M 护栏。
- **示例**：`ex16_dynamic` 动态按钮改用独立点击回调（修复复用画布回调导致的错位）；`ex02_widgets` 更正“grid 默认黑色”的错误注释；demo 键盘按钮删除冗余 `call()+fx_set_cb`。

### 文档
- `docs/guide.md` / `api.md` / `internals.md`：修正颜色表（16-bit vs 24-bit）、`fx_list_new_p`/`fx_drop_new_p` 第三参（页签页码）、默认缩放 2.5→1.6、默认背景 240→245、`uint16_t`→`fx_color_t`、`fx_delete` 用法（`fx_wptr`），并补写主题/滚动容器/GPU 光追三节。
- 新增画布教程说明于 `docs/examples.md`。

### 工程化
- 统一 `demo-main/Makefile`（demo/examples/test 单一源清单）。
- 无头单元测试 `demo-main/test/headless_test.c`（布局/命中/value/计数/查找删除/画布 API）。

## v2.1（历史）
- 桌面扩展完善、渲染引擎（光追/贴图/后处理）、动态压测、12 页演示。
