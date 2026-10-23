# fxtk 桌面演示（desktop）

`demo-main/app_desktop.c` 实现了一个多标签"桌面"演示，用于验收全部控件与交互。设计基线 480×272，等比缩放铺满任意分辨率（可全屏 1080P）。

## 页面结构

界面顶部为 `tab` 标签栏（选中页签**立体凸起**：亮底 + 高光 + 深字；未选中下凹），子页用 `page(n)` 挂载：

| 页 | 内容 | 验收点 |
|---|---|---|
| 波形 | 动画曲线 + 速度滑杆 + 开关 | `anim(1)` 每帧重绘、滑杆实时调参 |
| 图形 | 旋转贴图 + 矢量动效 + 拖尾 | `fx_draw_image_rot`、多边形、canvas 当容器 |
| 控件 | 3×3 网格键盘 + 进度联动 | `grid()` 布局、值变化回调 |
| 图片 | 贴图 + 缩放/灰度/染色 | `fx_image_set_zoom`、`fx_image_grayscale` |
| 3D | CPU/GPU 光线步进 | 多线程光追、GPU 通道切换、FPS 标签 |
| 输入 | 两个输入框 + 说明 | 键盘、Ctrl+A/C/V/X、`maxlen` 计数、光标像素级对齐、固定字号 |
| 画板 | 可绘制画布 + 笔色按钮 | 触摸/鼠标拖动绘制、离屏缓冲 |
| 键鼠 | 实时键鼠状态 | **悬停坐标实时更新**、按键回显 |
| 压测 | 移动控件 + **动态生长** | 帧率富余时自动 +1 控件，全屏可容数千 |
| 滚动 | 60 行虚拟滚动 | `fx_scroll_update` 核心滚动（目标+25%插值），滚动条滑块可拖拽 |
| 组件 | 列表/下拉/多字号 | 下拉向上弹、弹层滚动不越界 |
| 粒子 | 万级粒子动画 | 离屏缩半 + GPU blit 放大 |

## 压测页（动态控件生长）

- 标题实时显示 **总控件数**（`fxtk_widget_count()`）。
- 帧率 ≥30fps 时每帧创建一个紫色小按钮（9px 字号），位置按画布实际容量 24×20 格子自动排布。
- 新按钮和其他控件一样**正弦漂移**；布局列数随窗口尺寸变化。
- 全屏 1080P 下画布实际约 1753×810 → 可容纳 **2700+ 个**动态按钮（控件池 PC 端默认 8192）。
- 动态控件用 `fx_widget_fix()` 固定坐标，防止 `fx_layout()` 复位；指针缓存在 `s_dyn[]` 避免每帧 `fx_find`。

## 关键交互

- **立体标签页**：选中页签亮底深字 + 顶高光 + 底明线（凸起），未选中深底下凹，页签与内容区间有阴影分隔线。
- **右键菜单**：任意处右键弹出顶层菜单（置顶，不被页面遮挡），含复制/粘贴等项。
- **悬停**：鼠标移动（不按下）即更新坐标，键鼠页实时显示。
- **下拉**：靠底部自动向上弹；弹层带滚动；收起无"下巴"。
- **列表**：滚轮滚动 + 点击选中，选中行高亮。
- **拖动**：压测页控件可拖动，单遍绘制不闪烁。

## 布局写法

```c
/* 像素（480 设计坐标，等比缩放） */
fx_label_new(pixel("6,36","240,56"), page(5), title("..."));

/* 百分比（0.0~1.0 浮点字符串，相对父控件） */
fx_label_new(percent("0.02,0.01","0.98,0.09"), title("fxtk 演示 · 标签页"));
```

## v2.5：多真实窗口（实验性）

- 语义：**关闭任意窗口 = 退出整个程序**（Esc 同）；窗口位置/尺寸由应用负责（没有 WM 布局器）。
- **客户区对齐**：`fxtk_win_client_offset()` 返回标题栏/边框偏移；Win32 需扣掉，X11 返回 0（框架由 WM 负责）。
- **不要逐帧改窗口尺寸**：后端按尺寸缓存画布/帧缓冲，连续 resize 会打爆缓存并导致闪烁/panic。
- 多窗口串行 `SwapBuffers` 必须关闭 vsync，否则帧率被拖到 `60/N`。
- 完整说明/限制（X11 only、主屏、最大 8 窗口、输入未转发、真 HiDPI 未验证）见 [multi-window.md](multi-window.md)。

## 桌面/输入 API 逐条（`fxtk_desktop.h`）

| 函数 | 用法 |
|---|---|
| `fxtk_draw_textedit(w)` | 按框架规则绘制文本框（自绘复杂面板时复用）。 |
| `fx_set_focus(w)` / `fx_get_focus()` | 设置/读取键盘焦点控件。 |
| `fx_focus_blink()` | 光标闪烁亮态（自绘文本框用）。 |
| `fx_last_key()` | 最近一次按键（`fx_keyev_t`：utf8/key/down/mod）。 |
| `fx_touch_state(&x,&y,&pressed)` | 当前触摸状态。 |
| `fx_pressed()` | 当前按下的控件。 |
| `fx_wheel_take(w)` | 取滚轮事件（滚动容器）。 |
| `fx_scroll_content(w,h)` / `fx_scroll_update(w,h)` / `fx_scrollbar_draw(w,off,h)` / `fx_set_scroll(w,off)` | 滚动容器四件套：设内容高 → 每帧更新取偏移 → 画滚动条 → 拖拽设目标。 |
| `fx_text_width_n(s,n)` / `fx_draw_text_c_n(x,y,s,n,fg,bg)` | 量/画前 n 字符（编辑框/省略号）。 |
| `maxlen(n)` | 属性：文本框最大字符数。 |
| `fx_widget_set_rect(w,...)` / `fx_set_fgcolor(w,c)` | 设矩形 / 前景色（旧别名 `fx_set_fgcolor_w`）。 |

## v2.5：单窗口系统窗口行为（`fx_win_*`）

| 函数 | 用法 |
|---|---|
| `fx_win_available()` | 后端是否支持（sokol/sdl 桌面版为 1；stub 为 0）。 |
| `fx_win_set_borderless(on)` / `fx_win_get_borderless()` | 去/加系统标题栏；GNOME/Mutter 下 motif hints 可能无效。 |
| `fx_win_get_decorated()` | **回查**实际是否带系统装饰（不信任 set 返回值）。 |
| `fx_win_set_always_on_top(on)` / `fx_win_get_always_on_top()` | 置顶。 |
| `fx_win_move(x,y)` / `fx_win_get_pos(&x,&y)` / `fx_win_center()` | 移动/读位置/居中。 |
| `fx_win_drag_begin()` | 触发 WM 拖窗（自定义标题栏拖拽）。 |

> 实测边界（本机 GNOME+XWayland）：移动/居中精确 (0,0)、置顶生效、去标题栏无效。
> **不要**在 init_cb 里操作窗口；EWMH 请求要给 WM 处理时间。完整列表见 [api.md](api.md) 第 16 节。

### v2.5.1 输入语义（缺陷报告 H1/H2）

- `fx_textedit_set_readonly(w,1)` 现在**所有**改写路径都被拦截（键入/退格/删除/剪切/**Ctrl+V 粘贴**/ Ctrl+L 清空）。
- `fx_keyev_t.mod`：bit0=Ctrl、bit1=Shift；核心快捷键判断只认 bit0 —— **Shift+字母只产生文本**，不会被当成全选/粘贴/剪切。

