# 画布内窗口管理器（fxtk_wm）— 设计、接口与维护

> 这是 **单窗口框架内部**的"窗口管理"：把若干窗口渲染到离屏画布，再合成到当前窗口/画布上。
> 它和"系统真实多窗口后端（FXTK_MW）"是两回事：前者用 fxtk 直接画窗口外观，后者由操作系统创建窗口。
> 选型见文末对照表；真实多窗口见 [multi-window.md](multi-window.md)。

## 1. 它解决什么

想让界面看起来像"桌面上的多扇窗"（标题栏、边框、阴影、圆角、重叠、z 序），但**不想依赖系统窗口管理器**，
或需要把"窗口"当成普通元素做动画/缩放时，用 `fxtk_wm`：

- 窗口内容先渲染到 **每窗离屏画布**（分辨率 `cw × ch`），再作为图片合成到全屏画布；
- 于是标题栏/边框/焦点高光/圆角都直接画在合成画布上（背景已铺好，圆角外自然露出背景），**不需要 alpha 通道**；
- 分辨率无关：`fx_wm_set_ui(scale)` 统一缩放标题栏/边框/内容；
- 单进程、单窗口，天然可做任意位移动画、重叠、缩放。

## 2. 数据模型

每扇窗由 `fx_wm_add(title, style, cw, ch)` 创建，之后可设：

| 字段 | 设置方式 | 说明 |
|---|---|---|
| 标题 | `fx_wm_set_title(idx, s)` / add 参数 | 画在自绘标题栏上 |
| 样式 | `fx_wm_set_style(idx, st)` | `FX_WM_LINUX / FX_WM_WIN11 / FX_WM_TERM / FX_WM_PLAIN` |
| 位置/尺寸 | `fx_wm_set(idx, x,y,w,h)` | 窗口在合成画布上的矩形 |
| 层级 | `fx_wm_set_z(idx, z)` | 数值大者后画（在上层） |
| 焦点 | `fx_wm_set_focus(idx, f)` | 影响标题栏高亮 |
| 可见 | `fx_wm_set_visible(idx, v)` | 隐藏不参与合成 |
| 内容尺寸 | add 的 `cw/ch` | 内容回调里按这个尺寸绘制 |
| 旋转 | `fx_wm_set_rot(idx, deg)` | **当前忽略**（保留接口） |

## 3. 每帧顺序（必须按这个顺序）

    fx_wm_set_ui(ui_scale);                     // 可选：统一缩放
    int a = fx_wm_add("notes", FX_WM_WIN11, 640, 300);
    fx_wm_set(a, 40, 40, 720, 360);
    ...
    // 在画布回调 / 每帧绘制里：
    fx_wm_prepare(content_fn, ud);              // 兼容保留：空操作
    ...画桌面背景...
    fx_wm_draw(content_fn, ud);                 // 先渲染各窗内容到离屏画布，再按 z 序合成
    ...画歌词/叠加层（可选，会盖在窗口之上）...

`content_fn` 签名：`void fn(int idx, int cw, int ch, void *ud)` —— 用 `idx` 判断画哪扇窗，
在 `cw × ch` 里按窗口内容坐标系绘制；绘制期间框架会自动切换到该窗口的离屏画布。

## 4. API 一览（`fxtk_wm.h`）

| API | 说明 |
|---|---|
| `fx_wm_add(title, style, cw, ch)` | 新建一扇窗，返回下标（-1 失败） |
| `fx_wm_set(idx, x,y,w,h)` | 位置/尺寸 |
| `fx_wm_set_style / set_title / set_z / set_focus / set_visible / set_rot` | 属性 |
| `fx_wm_set_ui(k)` | 全局 UI 缩放（标题栏/边框/内容） |
| `fx_wm_prepare(content, ud)` | 兼容保留（空操作） |
| `fx_wm_draw(content, ud)` | 渲染所有窗口并合成 |
| `fx_wm_count()` / `fx_wm_get(...)` | 查询 |
| `fx_wm_title_h(style)` | 某样式的标题栏高度（自己画标题栏时用） |

## 5. 最小例子

    static void content(int idx, int cw, int ch, void *ud) {
        (void)ud;
        if (idx == 0) { fxtk_draw_text_c(6, 6, "notes", FX_RGB(230,237,243), FX_RGB(20,24,30)); }
        else          { fx_set_color(FX_RGB(40,90,140)); fx_fill_rect(0, 0, cw-1, ch-1); }
    }
    // 初始化
    fx_wm_set_ui(1.5f);
    fx_wm_add("notes", FX_WM_WIN11, 640, 300);
    fx_wm_add("player", FX_WM_LINUX, 1200, 70);
    // 每帧
    fx_wm_prepare(content, NULL);
    /* ...背景... */
    fx_wm_draw(content, NULL);

## 6. 限制与注意

- `fx_wm_set_rot()` 目前不生效（接口保留）；需要旋转请用 canvas 变换栈 `fx_canvas_push_affine()`。
- `fx_wm_prepare()` 是兼容空操作，真正渲染在 `fx_wm_draw()` 里。
- 窗口内容尺寸（cw/ch）在 add 时确定；改尺寸需重建窗口。
- 这是"画出来的窗口"，**没有**系统标题栏/任务栏/窗口管理器行为（拖动/最小化等需自己做）；
  每扇窗也不占独立系统窗口、不参与 OS 焦点。
- 离屏画布数量与内存随窗口数/内容尺寸增长；大分辨率和多窗口时注意内存。

## 7. 选型：fxtk_wm vs FXTK_MW（系统真实多窗口）

| 需求 | 用 `fxtk_wm`（本文件） | 用 `FXTK_MW`（[multi-window.md](multi-window.md)） |
|---|---|---|
| 外观像桌面窗口（标题栏/边框/圆角） | ✅ 自绘，完全可控 | ✅ 由系统绘制 |
| 单窗口应用内叠放/动画/缩放 | ✅ 最简单 | ❌ 不适用 |
| 需要系统真实窗口（任务栏、Alt+Tab、独立焦点） | ❌ 画出来的 | ✅ 一个进程 N 个原生窗口 |
| 跨平台窗口行为一致 | ✅ 自己画，一致 | ⚠️ X11/Win32 各自实现，需对齐 |
| 复杂度/依赖 | 低（纯 fxtk 绘制） | 高（平台层 + GL 上下文/事件） |
