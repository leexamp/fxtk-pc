# fxtk API 参考（v2.5）

> 本文件**手工维护**，与 `components/fxtk/*.h` 同步更新（CI 用 `tools/check_api_docs.py` 检查"头文件里每个公开函数都在本文出现"）。
> 每个条目：**签名** + **用法/语义** + 必要时的返回值/注意事项。
> 相关文档：[guide.md](guide.md)（教程）· [backends.md](backends.md)（后端服务）· [internals.md](internals.md)（内部/驱动）·
> [multi-window.md](multi-window.md)（系统真实多窗口）· [window-manager.md](window-manager.md)（画布内窗口管理器）。

## 0. 约定

- **坐标**：客户区左上角为原点，向右下为正。矢量图元用 `(x1,y1,x2,y2)` **闭区间**（包含右/下边界）；
  控件/画布回调里另有 `(x,y,w,h)` 形式。像素坐标是**逻辑像素**（窗口客户区），后端负责 DPI/缩放。
- **颜色**：`fx_color_t` = `0xAARRGGBB`。`FX_RGB(r,g,b)` 的 alpha 位为 0，按**不透明**处理（兼容旧代码）；
  要半透明用 `FX_RGBA(r,g,b,a)`；`fx_color_alpha(c)` 取出有效 alpha（0→255）。
- **绘制上下文**：所有立即模式绘制必须在 `fx_frame_begin()` 与 `fx_frame_end()` 之间（或在 `fx_canvas_begin/end`、
  控件回调、`fx_wm_draw` 的内容回调里）。绘制只对"当前目标"生效（窗口画布 / 离屏画布）。
- **控件**：用属性宏（C99 compound literal）创建，末尾自动补 `FX_ATTR_END`；例如
  `fx_widget_t *b = fx_button_new(name("ok"), text("确定"), x(20), y(20), w(120), h(44), call(on_ok));`
- **缩放**：框架按 `fx_ui_scale()` 自动缩放控件；`fx_set_max_scale()` 限制上限；避免在业务里手算倍数。
- **能力查询**：后端可选能力（GPU 圆角、raymarch、图片 GPU 形变等）先查 `fx_*_available()`；缺失时是安全空操作。
- **版本**：本文对应 v2.5；多窗口（`fxtk_win_*`）需要独立后端 `-DFXTK_MW`。

---

## 1. 颜色与主题

| 函数/宏 | 用法 |
|---|---|
| `FX_RGB(r,g,b)` | 24 位不透明颜色，例 `FX_RGB(33,150,243)`。 |
| `FX_RGBA(r,g,b,a)` | 带 alpha（1–255）的颜色；用于半透明叠加/阴影。 |
| `FX_TEXT_NOBG` | 文字专用：让字形背景完全透明（stb 字体驱动识别）。 |
| `fx_color_alpha(c)` | 返回颜色的有效 alpha（alpha=0 视为 255）。 |
| `fx_set_color(c)` | 设置后续立即模式图元的颜色。 |
| `fx_set_bg(c)` | 设置窗口/画布清屏背景色。 |
| `fx_get_bg()` | 读取当前背景色。 |
| `fx_set_global_background(c)` | 设置所有控件默认背景（全局主题底）。 |
| `fx_set_dark_theme(dark)` / `fx_is_dark_theme()` | 切深/浅色主题并查询。 |
| `fx_colorx_current(c)` | 取主题化颜色 `fx_colorx_t` 在当前主题下的实际 `fx_color_t`。 |
| 预置色 `FX_RED/FX_GREEN/FX_BLUE/FX_WHITE/FX_BLACK/FX_YELLOW/FX_CYAN/FX_MAGENTA/FX_GRAY/FX_LGRAY` | 常用纯色。 |
| 主题色 `FX_WINDOW_BG/FX_WINDOW_DARK/FX_UI_FG/FX_BTN_BLUE/FX_OK_GREEN/FX_RED_ACCENT/FX_PURPLE/FX_CANVAS_DARK` | 与默认配色一致的语义色。 |

---

## 2. 控件创建（属性宏）

属性函数返回 `fx_attr_t`，作为构造宏的参数：`fx_xxx_new(...)`。

| 属性 | 用法 |
|---|---|
| `name("id")` | 控件名（`fx_find` 用）。 |
| `text("...")` | 显示文字（标签/按钮/文本框初值）。 |
| `title("...")` | 标题（面板/窗口类控件）。 |
| `x(v) / y(v) / w(v) / h(v)` | 位置尺寸（逻辑像素）；不写则由布局决定。 |
| `color(c)` | 控件主色（填充/强调）。 |
| `fgcolor(c)` | 前景/文字色（v2.3 起独立设置）。 |
| `line(n) / row(n)` | 行/列步进位置（古早布局辅助）。 |
| `grid1(name) / grid5(name,r1,c1,r2,c2)` | 网格定位（按 1/5 分格）。 |
| `percent("50%")` | 百分比尺寸/位置（相对父容器）。 |
| `pixel("12")` | 固定像素尺寸/位置（显式禁自动缩放）。 |
| `call(cb)` | 事件回调 `fx_cb_t`（按钮点击/滑条变化等）。 |
| `maxlen(n)` | 文本框最大字符数。 |
| `image(img)` | 给图片控件绑定 `fx_image_t*`。 |
| `fx_par_for(cnt)` | 并行循环宏：`fx_par_for(1000) { ... }`，循环体可并行。 |

| 构造宏 | 创建的控件 |
|---|---|
| `fx_widget_new_impl(type, attrs[])` | 通用构造（type 见第 14 节枚举）；其余 `*_new` 都是它的薄封装。 |
| `fx_button_new(...)` | 按钮。 |
| `fx_label_new(...)` | 文本标签。 |
| `fx_panel_new(...)` | 面板/容器。 |
| `fx_canvas_new(...)` | 自定义画布（回调里用 `fx_canvas_*` 绘制）。 |
| `fx_slider_new(...)` | 滑条（值用 `fx_set_value/fx_get_value`）。 |
| `fx_progress_new(...)` | 进度条。 |
| `fx_checkbox_new(...)` | 复选框。 |
| `fx_tab_new(...)` | 分页容器（配合 tab 页属性）。 |
| `fx_grid_map(...)` | 网格图片/地图容器。 |
| `fx_list_new(rows, cols)` / `fx_drop_new(rows, cols)` | 列表 / 下拉框（`fx_list_add` 加行、`fx_list_set_cb`、`fx_list_sel`）。 |

---

## 3. 控件树、查询与属性

| 函数 | 用法 |
|---|---|
| `fx_parent(p)` | 把它之后创建的控件挂到父容器 `p` 下。 |
| `fxtk_unlink(w)` | 从父链表摘除（不销毁），便于重新挂载/排序。 |
| `fxtk_raise(w)` | 提到最前（最后绘制，置顶）。 |
| `fxtk_lower(w)` | 压到最底（最先绘制）。 |
| `fx_find("id")` | 按名字查控件，找不到返回 NULL。 |
| `fx_delete(...)` | 按属性宏删除控件，例 `fx_delete(name("ok"));`；子控件递归删除。 |
| `fx_set_title(w,s)` / `fx_widget_title(w)` | 设置/读取标题。 |
| `fx_set_color_w(w,c)` | 设置控件主色。 |
| `fx_set_fgcolor(w,c)` | 设置控件前景/文字色。 |
| `fx_set_value(w,v)` / `fx_get_value(w)` | 设置/读取数值（滑条、进度、复选）。 |
| `fx_set_cb(w,cb,ud)` | 运行期改回调。 |
| `fx_set_visible(w,vis)` | 显示/隐藏。 |
| `fx_widget_type(w)` | 控件类型（`FX_W_*`）。 |
| `fx_widget_set_rect(w,x1,y1,x2,y2)` / `fx_widget_rect(w,&x1,&y1,&x2,&y2)` | 设置/读取客户矩形。 |
| `fx_set_fontsize(w,size)` | 控件字号。 |
| `fx_set_align(w,a)` | 文字对齐（0 左、1 中、2 右）。 |
| `fx_set_image(w,img)` / `fx_image_set_zoom(w,pct)` | 图片控件的图/缩放。 |
| `fx_set_fit(w,on)` / `fxtk_apply_fit(w)` | 让控件按内容自适应尺寸。 |
| `fxtk_fit_rect(w,&x1,&y1,&x2,&y2)` | 读取自适应后的矩形。 |
| `fx_set_max_scale(f)` / `fxtk_max_scale1000()` | 控件缩放上限（倍数 / 千分比）。 |
| `fxtk_set_ui_scale_cap(p)` / `fxtk_ui_scale()` | 全局 UI 缩放上限 / 当前缩放。 |
| `fx_widget_fix(w,x,y)` | 固定控件在坐标（跳过自动布局）。 |

---

## 4. 布局

| 函数 | 用法 |
|---|---|
| `fx_layout()` | 立即执行一次布局（属性/尺寸变更后；通常框架每帧自动做）。 |
| `fx_flex_solve(cx1,cy1,cx2,cy2,dir,gap,pad,justify,align,items,n,out_xy4)` | flex 求解：输入容器矩形与条目，输出每项 `(x1,y1,x2,y2)`；`dir`=FX_FLEX_ROW/COLUMN。 |
| `fx_flex_apply(container,items,n,dir,gap,pad,justify,align)` | 求解并直接套到控件数组上。 |
| `fx_flex_apply_simple(container,dir,gap,pad,justify,align)` | 自动取容器的子控件做 flex 布局。 |
| `FX_POS_FLEX` | 坐标模式：由 `fx_flex_apply` 写入，布局期不重算。 |

---

## 5. 输入 / 焦点 / 文本编辑

| 函数 | 用法 |
|---|---|
| `fx_touch_press(x,y)` / `fx_touch_move(x,y)` / `fx_touch_release(x,y)` | 把触摸/鼠标事件喂给框架（驱动调用，或自己模拟点击）。 |
| `fx_touch_state(&x,&y,&pressed)` | 查询当前触摸状态。 |
| `fx_set_focus(w)` / `fx_get_focus()` | 设置/读取键盘焦点控件。 |
| `fx_focus_blink()` | 焦点光标是否处于闪烁亮态（自绘文本框用）。 |
| `fx_last_key()` | 最近一次按键事件。 |
| `fx_pressed()` | 当前按下的控件。 |
| `fx_wheel_take(w)` | 取走滚轮事件（滚动容器用）。 |
| `fxtk_draw_textedit(w)` | 按框架规则绘制一个文本框（自绘复杂面板时用）。 |
| `fx_textedit_set_readonly(w,ro)` | 文本框只读。 |
| `fx_textedit_text(w)` | 取文本框内容（可能 NULL/空串）。 |
| `fx_draw_text_c_n(x,y,s,n,fg,bg)` / `fx_text_width_n(s,n)` | 只画/量前 n 个字符（文本编辑/省略号）。 |
| `fx_set_ime_pos(x,y)` | 输入法候选窗位置（屏幕坐标；后端实现 ime_pos 钩子时生效）。 |
| `fx_set_touch_debug(on)` | 触摸调试可视化。 |

---

## 6. 帧、重绘与背景

| 函数 | 用法 |
|---|---|
| `fx_init(drv)` | 绑定驱动并初始化框架（应用启动一次）。 |
| `fx_poll()` | 每帧处理输入/布局/绘制（驱动事件循环里调）。 |
| `fx_frame_begin()` / `fx_frame_end()` | 手动控制一帧绘制区间（画布/自绘/多窗口时用）。 |
| `fx_repaint()` / `fx_repaint_rect(x1,y1,x2,y2)` | 请求整帧/局部重绘（脏区）。 |
| `fx_set_autorepaint(on)` | 每帧自动重绘（默认开；自绘/离屏时可关）。 |
| `fx_band_index()` | 当前扫描带序号（分带渲染/调试）。 |
| `fx_set_grid_lines(on)` / `fxtk_grid_lines_on()` | 布局网格线显示开关。 |
| `fx_screenshot(path)` | 回读当前帧存 PNG（驱动需 read_pixels 钩子）。 |
| `fx_set_window_title(s)` | 设置系统窗口标题（单窗口后端）。 |
| `fxtk_set_fps_debug(on)` / `fxtk_fps()` | 调试 FPS 显示 / 读取实测 FPS。 |
| `fxtk_widget_count()` | 当前存活控件数（调试/泄漏检查）。 |
| `fxtk_drv_width()` / `fxtk_drv_height()` | 驱动画布像素尺寸。 |
| `fx_width()` / `fx_height()` | 当前绘制目标逻辑宽高。 |

---

## 7. 立即模式绘制（基础图元）

所有图元使用当前颜色（`fx_set_color`）与当前裁剪（`fx_set_clip`）。

| 函数 | 用法 |
|---|---|
| `fx_set_clip(x1,y1,x2,y2)` / `fx_reset_clip()` | 设置/清除裁剪矩形（闭区间）。 |
| `fx_draw_pixel(x,y)` | 画一个像素。 |
| `fx_draw_hline(x1,x2,y)` / `fx_draw_vline(x,y1,y2)` | 水平/垂直 1px 线。 |
| `fx_draw_line(x1,y1,x2,y2)` | 任意直线。 |
| `fx_draw_rect(x1,y1,x2,y2)` / `fx_fill_rect(...)` | 矩形描边 / 填充。 |
| `fx_fill_rect_gradient(x1,y1,x2,y2,c1,c2,vertical)` | 线性渐变填充（`vertical=1` 上下，0 左右）。 |
| `fx_draw_rect_round(...,r)` / `fx_fill_rect_round(...,r)` | 圆角矩形描边 / 填充（`r` 圆角半径）。 |
| `fx_draw_circle(cx,cy,r)` / `fx_fill_circle(...)` | 圆描边 / 填充。 |
| `fx_draw_ellipse(cx,cy,rx,ry)` / `fx_fill_ellipse(...)` | 椭圆描边 / 填充。 |
| `fx_draw_arc(cx,cy,r,a1,a2)` / `fx_fill_arc(...)` | 圆弧 / 扇形（角度制，0°=右，顺时针）。 |
| `fx_draw_triangle(x1,y1,x2,y2,x3,y3)` / `fx_fill_triangle(...)` | 三角形描边 / 填充。 |
| `fx_draw_polygon(pts,n)` / `fx_fill_polygon(pts,n)` | 多边形描边 / 填充（`pts` 为 `int16_t` 数组，`x,y` 交错，n=点数）。 |
| `fx_set_aa(level)` | 画布 CPU 抗锯齿 0/1（慢，默认关）。 |
| `fx_set_widget_aa(level)` | 控件/图元 GPU SDF 抗锯齿档位 0/1/2（默认 1）。 |
| `fx_widget_aa_level()` / `fxtk_aa()` / `fx_aa_autodegrade()` | 查询 AA 档位 / 画布 AA 开关 / 掉帧时降一档。 |
| `fx_animation(on)` / `fx_animation_enabled()` | 全局控件动画开关（默认关，保证金图确定性）。 |

---

## 8. 文本

| 函数 | 用法 |
|---|---|
| `fx_draw_text(x,y,s)` | 用当前颜色/默认字号画一行文本。 |
| `fx_draw_text_c(x,y,s,fg,bg)` | 指定前景/背景色画一行文本（`bg=FX_TEXT_NOBG` 透明底）。 |
| `fx_text_width(s)` | 量文本像素宽度（默认字号）。 |
| `fxtk_font_size(size)` | 取指定字号字体句柄（缓存）。 |
| `fxtk_font_set_size(size)` | 切默认字号。 |
| `fxtk_text_width_size(size,t)` / `fxtk_font_height(size)` | 指定字号的宽/高。 |
| `fxtk_draw_text_size(size,x,y,t,fg,bg)` | 指定字号画文本（MV/多窗口逐帧文字用这个，避免反复切字号）。 |
---

## 9. 画布（离屏）与 GPU 钩子

| 函数 | 用法 |
|---|---|
| `fx_canvas_begin(cv)` / `fx_canvas_end()` | 在 `fx_canvas_new` 控件回调里开始/结束绘制：begin 后原点变画布左上，end 恢复。 |
| `fx_canvas_enable_buf(cv)` / `fx_canvas_set_buf(w,on)` | 启用/关闭画布离屏缓冲：开启后每帧内容可保留（适合逐帧覆盖绘制的频谱/动画）。返回是否成功（内存不足会失败）。 |
| `fx_canvas_size(cv,&w,&h)` | 画布本地宽高（回调内取代写死尺寸）。 |
| `fx_canvas_clear(cv,color)` | 一键把画布清成指定颜色（省去整屏 fill_rect 样板）。 |
| `fx_raymarch_available()` | 后端是否支持 GPU 实时光线步进（sokol 后端有该钩子时为 1）。 |
| `fx_draw_raymarch(time,x,y,w,h)` | 在指定矩形里画 GPU 光线步进场景；`time` 驱动动画；后端不支持时空操作。 |
| `fx_quad_warp_gpu()` | 图片四角形变是否由 GPU 钩子接管（HUD/性能提示用）。 |

---

## 10. 2D 仿射变换 / 四边形 / 图片

| 函数 | 用法 |
|---|---|
| `fx_canvas_push_affine(a,b,c,d,e,f)` | 压入 2D 仿射：`p' = M·p`，M = [[a,c,e],[b,d,f],[0,0,1]]；之后图元/图片都变换（矩形→实心四边形，图片→透视四边形）。栈深 8。 |
| `fx_canvas_pop_affine()` | 弹出栈顶变换。 |
| `fx_transform_reset()` | 清空变换栈（每帧自动复位）。 |
| `fx_transform_depth()` | 当前栈深（调试/断言）。 |
| `fx_canvas_transform_point(x,y,&ox,&oy)` | 把点过一遍当前变换（栈空原样返回），用于命中测试。 |
| `fx_fill_quad(xy8)` | 四角（8 个 float：x1,y1..x4,y4 顺时针）实心四边形填充，与图片透视同一套单应。 |
| `fx_draw_image(img,x,y,dw,dh)` | 画图，缩放到 `dw×dh`（不传图像尺寸时按 1:1 需自行换算）。 |
| `fx_draw_image_ex(img,x,y,dw,dh,dark)` | 同上；`dark=1` 画暗色版（禁用态/背景图）。 |
| `fx_draw_image_quad(img,xy8)` | 透视四边形贴图（单应变换），用于 3D 翻转/自由形变。 |
| `fx_draw_image_quad_persp(img,x1,y1,x2,y2,top_inset,top_shift)` | 快捷透视：上边内缩/平移，一屏出"斜视屏幕"效果。 |
| `fx_quad_homography(src8,dst8,m9)` | 求 4 点→4 点的 3×3 单应矩阵（成功 1）。 |
| `fx_quad_corner_weights(xy8,d4)` | 求四角透视权重（拖动角手柄时算 w 用）。 |
| `fx_mat3_invert(m,out)` | 3×3 矩阵求逆（成功 1）。 |

---

## 11. 滚动 / 列表 / 下拉

| 函数 | 用法 |
|---|---|
| `fx_scroll_content(w,content_h)` | 设置滚动容器内容总高（触发滚动条/惯性）。 |
| `fx_scroll_update(w,content_h)` | 滚动容器每帧调用：返回可滚动偏移（内部做缓动），并处理滚轮/拖拽。 |
| `fx_scrollbar_draw(w,off,content_h)` | 自绘滚动条（`off` 当前偏移）。 |
| `fx_set_scroll(w,off)` | 设置滚动偏移（拖滚动条时用；走同一套缓动）。 |
| `fx_list_new(r1,r2)` / `fx_drop_new(r1,r2)` | 新建列表/下拉框（`r1/r2` 为两行区标题）。 |
| `fx_list_add(w,t)` / `fx_drop_add(w,t)` | 追加一行。 |
| `fx_list_set_cb(w,cb)` | 行点击回调 `cb(widget, ud)`。 |
| `fx_list_sel(w)` | 当前选中行号（-1 无）。 |
| `fx_list_clear(w)` | 清空列表（重建前用）。 |
| `fx_list_new_p(r1,r2,pg)` / `fx_drop_new_p(r1,r2,pg)` | 带分页的列表/下拉（`pg` 每页行数）。 |

---

## 12. 手势（单指 / 触摸）

| 函数 | 用法 |
|---|---|
| `fx_gesture_feed(x,y,pressed,now_ms)` | 把触摸采样喂给手势识别（驱动每帧调；也可用于模拟）。 |
| `fx_gesture_poll(ev)` | 取出手势事件（点击/双击/长按/滑动等），成功 1。 |
| `fx_gesture_pending()` | 还有待处理手势事件？ |
| `fx_gesture_active()` | 当前是否有手势正在识别。 |
| `fx_gesture_dropped()` | 被丢弃的采样数（调试）。 |
| `fx_gesture_reset()` | 清空识别状态。 |
| `fx_gesture_name(type)` | 手势类型名（调试/日志）。 |
| `fx_gesture_config(long_press_ms,tap_max_ms,tap_max_dist,swipe_min_dist,double_tap_ms)` | 调整识别阈值。 |
| `fx_gesture_config_default()` | 恢复默认阈值。 |

---

## 13. CPU 并行

| 函数 | 用法 |
|---|---|
| `fx_parallel_for(n, body, ud)` | 把 `[0,n)` 分块到多线程；`body(lo,hi,ud)` 处理半开区间。**CPU 多线程，不是 GPU**。 |
| `fx_parallel_threads()` / `fx_parallel_set_threads(n)` | 查询/设置线程数（0=自动）。 |
| `fx_par_for(cnt)` | 语法糖：串行循环，零开销，仅名字像并行；循环变量 `fx_i`。 |

---

## 14. SVG（子集解析 → canvas）

| 函数 | 用法 |
|---|---|
| `fx_svg_parse(text)` | 从内存字符串解析 SVG，返回 `fx_svg_t*`（失败 NULL）。 |
| `fx_svg_load(path)` | 从文件加载并解析。 |
| `fx_svg_free(s)` | 释放。 |
| `fx_svg_draw(s,x,y,w,h)` | 绘制到当前目标（缩放到 w×h）。 |
| `fx_svg_shape_count(s)` | 解析出的图元数量（自检/调试）。 |
| `fx_svg_shape_points(s,i)` | 第 i 个图元的点数。 |
| `fx_svg_shape_bbox(s,i,&x1,&y1,&x2,&y2)` | 第 i 个图元的包围盒。 |
| `fx_svg_skipped(s)` | 不支持而跳过的元素数（诚实汇报，便于判断兼容性）。 |
| `fx_svg_size(s,&vw,&vh)` | SVG 声明的 viewBox 尺寸。 |

---

## 15. 后端服务（fxtk_backends.h）

### 15.1 能力 / 屏幕 / 日志

| 函数 | 用法 |
|---|---|
| `fx_backend_caps()` | 后端能力位（`fx_caps_t`，可用于判断 GPU 圆角/图片/剪贴板等）。 |
| `fx_backend_name()` | 后端名（"sokol"/"sdl"/"stub"…，日志/诊断）。 |
| `fx_backend_set_screen(w,h,dpi_scale)` | 驱动启动时告知物理屏幕尺寸与 DPI 缩放。 |
| `fx_screen_w()` / `fx_screen_h()` / `fx_dpi_scale()` | 屏幕像素尺寸 / DPI 缩放。 |
| `fx_ui_scale_for(phys_w,dpi_scale,cap,compact)` | 由物理宽/DPI 计算 UI 缩放（`cap` 上限百分比，`compact` 紧凑模式）。 |
| `fx_log_set_level(lv)` / `fx_log_get_level()` | 日志等级（`fx_log_level_t`）。 |
| `fx_log(lv,fmt,...)` | 打日志（带等级/颜色；stub 后端会过滤）。 |

### 15.2 随机 / 时间

| 函数 | 用法 |
|---|---|
| `fx_rand_seed(seed)` | 设定种子（可复现：金图/测试必须种）。 |
| `fx_rand_u32()` / `fx_rand_below(bound)` / `fx_rand_range(lo,hi)` / `fx_randf()` | 32 位随机 / `[0,bound)` / `[lo,hi]` / `[0,1)`。 |
| `fx_time_us()` / `fx_time_ms()` | 单调微秒/毫秒（动画计时用，不受墙钟跳变影响）。 |
| `fx_time_unix()` | Unix 秒（墙钟）。 |
| `fx_sleep_ms(ms)` | 睡眠毫秒。 |

### 15.3 路径 / 文件 / 目录

| 函数 | 用法 |
|---|---|
| `fx_path_join(dst,cap,a,b)` | 拼路径（自动处理分隔符），返回写入长度。 |
| `fx_path_basename(path)` / `fx_path_ext(path)` | 取文件名 / 扩展名（返回内部/静态缓冲，勿释放）。 |
| `fx_path_dir(dst,cap,path)` | 取所在目录。 |
| `fx_dir_app(dst,cap)` | 应用数据目录（偏好/日志放这里）。 |
| `fx_file_exists(path)` / `fx_file_size(path,&sz)` | 存在性 / 64 位大小。 |
| `fx_file_read(path,&sz)` / `fx_file_free(buf)` | 读整个文件到 malloc 内存（失败 NULL），配套释放。 |
| `fx_file_write(path,data,size,append)` | 写文件（append=1 追加）。 |
| `fx_dir_create(path)` / `fx_dir_list(dir,out,max)` | 建目录 / 列目录（`fx_dir_entry_t` 数组，返回条数）。 |

### 15.4 图片 / 剪贴板 / 偏好 / 系统对话框

| 函数 | 用法 |
|---|---|
| `fx_img_load_file(path,&img)` / `fx_img_load_mem(data,size,&img)` | 解码 PNG/JPEG/BMP/GIF/TGA/PNM 到 `fx_img_t`。 |
| `fx_img_free(&img)` | 释放解码结果。 |
| `fx_img_has_file_ext(path)` | 后缀是否可解码图片（文件选择器过滤用）。 |
| `fx_img_save_png(path,px,w,h,channels)` | 把像素保存成 PNG（`channels`=3/4）。 |
| `fx_clip_set(text)` / `fx_clip_get(buf,cap)` | 系统剪贴板写入/读取（后者返回长度）。 |
| `fx_prefs_set(key,val)` / `fx_prefs_get(key,buf,cap)` / `fx_prefs_save()` / `fx_prefs_load()` | 键值偏好：设置/读取/落盘/读盘。 |
| `fx_backend_pick_dir(out,cap)` | 系统选目录对话框（返回 1 选择成功）。 |
| `fx_backend_pick_file(out,cap,title,ext_csv)` | 系统选文件对话框（`ext_csv` 形如 "png,jpg"）。 |
| `fx_open_url(url)` / `fx_reveal_file(path)` | 浏览器打开 URL / 文件管理器定位文件。 |
| `fx_cpu_count()` | CPU 逻辑核心数（决定并行线程默认值）。 |

---

## 16. 桌面 / 单窗口系统行为（fxtk_desktop.h + v2.5 fx_win_*）

| 函数 | 用法 |
|---|---|
| `fx_win_available()` | 当前后端是否支持系统窗口操作（单窗口；独立多窗口见第 18 节）。 |
| `fx_win_set_borderless(on)` / `fx_win_get_borderless()` | 去/加系统标题栏。**GNOME/Mutter 下 motif hints 常无效**，用 `fx_win_get_decorated` 回查实际结果。 |
| `fx_win_set_always_on_top(on)` / `fx_win_get_always_on_top()` | 置顶/取消置顶。 |
| `fx_win_get_decorated()` | 回查是否真的有系统装饰（1=有边框，0=无边框）。 |
| `fx_win_move(x,y)` / `fx_win_get_pos(&x,&y)` | 移动窗口/读位置。 |
| `fx_win_center()` | 居中到当前屏幕。 |
| `fx_win_drag_begin()` | 让 WM 进入"拖窗口"状态（自定义标题栏拖拽用）。 |

> 铁律：**不要在 init_cb 里操作窗口**（WM 初始摆放会覆盖你）；EWMH 请求要留处理时间。
> 详见 [desktop.md](desktop.md) 与 CHANGELOG v2.5。

---

## 17. 用户级特效 / 图片处理

| 函数 | 用法 |
|---|---|
| `fx_draw_image_rot(img,cx,cy,angle_deg,scale_pct)` | 以中心旋转绘制图片（CPU 路径，缩放 percent）。 |
| `fx_fill_polygon_rot(pts,n,cx,cy,angle_deg)` | 绕中心旋转填充多边形。 |
| `fx_image_flip_x(img)` / `fx_image_flip_y(img)` | 原地水平/垂直翻转像素。 |
| `fx_image_grayscale(img)` | 原地灰度化。 |
| `fx_image_tint(img,c,amount)` | 按强度给图片着色（0–100）。 |
| `fx_image_brightness(img,delta)` | 调整亮度（±）。 |
| `image(img)` | 属性宏：把 `fx_image_t*` 绑到图片控件。 |
| `fx_image_create(w,h)` / `fx_image_free(img)` | 创建/销毁内存图片。 |
| `fx_image_set_px(img,x,y,c)` | 写一个像素（生成程序化纹理）。 |
| `fx_image_load(path)` | 从文件加载图片。 |
| `fx_set_image(w,img)` / `fx_image_set_zoom(w,pct)` | 给控件设图/缩放。 |

---

## 18. 系统真实多窗口（fxtk_win.h，需 -DFXTK_MW）

| 函数 | 用法 |
|---|---|
| `fxtk_win_init()` / `fxtk_win_shutdown()` | 初始化/关闭多窗口后端（应用启动/退出各一次）。 |
| `fxtk_win_create(w,h,x,y,title)` | 创建窗口，返回 id（失败 -1）。 |
| `fxtk_win_create_ex(w,h,x,y,title,flags)` | 同上带标志；`FXTK_WIN_BORDERLESS` = 无边框。 |
| `fxtk_win_show(id,on)` / `fxtk_win_raise(id)` | 显示/隐藏、置顶。 |
| `fxtk_win_move(id,x,y)` / `fxtk_win_resize(id,w,h)` | 移动/改客户区尺寸（**不要逐帧 resize**，会打爆画布缓存）。 |
| `fxtk_win_title(id,s)` | 改窗口标题（UTF-8）。 |
| `fxtk_win_begin(id)` / `fxtk_win_end(id)` | 该窗口一帧的绘制区间（begin 切上下文，end 合成+present）。 |
| `fxtk_win_width(id)` / `fxtk_win_height(id)` | 客户区像素尺寸。 |
| `fxtk_win_poll()` / `fxtk_win_quit()` / `fxtk_win_time()` | 事件轮询 / 是否退出 / 单调时间。 |
| `fxtk_win_screen(&w,&h)` | 屏幕像素尺寸（舞台等比缩放/居中用）。 |
| `fxtk_win_client_offset(id,&ox,&oy)` | 客户区相对外框偏移（Win32 扣标题栏；X11 返回 0）。 |

> 语义/限制：关闭任意窗口 = 退出；X11 only、主屏、最大 8 窗口、输入事件未转发控件层、真 HiDPI 未验证。
> 完整设计见 [multi-window.md](multi-window.md)。

---

## 19. 画布内窗口管理器（fxtk_wm.h）

| 函数 | 用法 |
|---|---|
| `fx_wm_add(title,style,cw,ch)` | 新建窗口，返回下标；`style`=FX_WM_LINUX/WIN11/TERM/PLAIN。 |
| `fx_wm_set(idx,x,y,w,h)` | 窗口位置/尺寸（合成画布坐标）。 |
| `fx_wm_set_style/set_title/set_z/set_focus/set_visible/set_rot` | 属性；`z` 大者在上，`rot` 暂未实现。 |
| `fx_wm_set_ui(k)` | 全局缩放（标题栏/边框/内容）。 |
| `fx_wm_prepare(content,ud)` | 兼容保留，空操作。 |
| `fx_wm_draw(content,ud)` | 渲染所有窗口并合成；`content(idx,cw,ch,ud)` 画每窗内容。 |
| `fx_wm_count()` / `fx_wm_get(...)` | 数量 / 读属性。 |
| `fx_wm_title_h(style)` | 某样式标题栏高度。 |

> 完整用法/选型（与系统多窗口的区别）见 [window-manager.md](window-manager.md)。

---

## 20. 驱动接口 fx_driver_t（写后端时）

应用不直接碰它；写后端时把函数指针填进 `fx_driver_t` 并传 `fx_init()`。除 `width/height` 外**全部可为 NULL**（框架会走 CPU 回退）。

| 字段 | 契约 |
|---|---|
| `width, height` | 当前画布尺寸（像素）。 |
| `init` | 后端初始化；返回 0 失败。 |
| `set_window(x0,y0,x1,y1)` | 通知后端当前绘制窗口（用于裁剪/统计）。 |
| `push_pixels(px,n)` | 推像素流（软后端用）。 |
| `hold_begin/hold_end` | 批量绘制包裹（GPU 合批提示）。 |
| `fill_rect` | GPU/CPU 矩形填充（核心路径）。 |
| `touch_read(&x,&y,&pressed)` | 读触摸/鼠标（返回 1 有事件）。 |
| `key_read(ev)` | 读键盘（`fx_keyev_t`）。 |
| `clip_set/clip_get` | 系统剪贴板。 |
| `set_title` | 设置窗口标题。 |
| `wheel_read(&x,&y,&dy)` | 读滚轮。 |
| `blit_img` | 缩放 blit 图片（`dark` 暗色）。 |
| `set_clip_rect` | 设置裁剪。 |
| `fill_tri / draw_line` | GPU 三角/折线批处理。 |
| `blit_tex` | 文字纹理 blit（src/dst）。 |
| `blit_img_rot` | 旋转 blit。 |
| `read_pixels` | 回读当前帧（截图/金图回归）。 |
| `draw_image_quad` | GPU 真透视四边形（无则 CPU 逆单应）。 |
| `raymarch` | GPU 光线步进到矩形。 |
| `fill_rect_round / stroke_rect_round` | GPU SDF 圆角矩形填充/描边。 |
| `draw_line_aa` | GPU 羽化线段。 |
| `ime_pos` | 输入法候选窗位置。 |

> 框架内部的调度/缓存/多窗口细节见 [internals.md](internals.md)。

---

## 21. 枚举与类型速查

- 控件类型 `FX_W_BUTTON/LABEL/PANEL/CANVAS/SLIDER/PROGRESS/CHECKBOX/TAB/GRID/IMAGE/TEXTEDIT/SCROLL`（见 `FX_W_*` 与 `fx_widget_type()`）。
- 主题色类型 `fx_colorx_t`（语义色，用 `fx_colorx_current()` 取当前主题值）。
- 按键 `fx_keyev_t` / `FX_KEY_*`；手势事件 `fx_gesture_ev_t`；日志 `fx_log_level_t`。
- 图片 `fx_img_t`（解码后）/ `fx_image_t`（框架内图片对象，可写像素）。
- SVG `fx_svg_t`；flex 条目 `fx_flex_item_t`；目录项 `fx_dir_entry_t`。
- 多窗口标志 `FXTK_WIN_BORDERLESS`；画布内窗口样式 `FX_WM_LINUX/WIN11/TERM/PLAIN`。

---

## 22. 最小主循环（单窗口）

```c
static void on_frame(void) {
    fx_frame_begin();
    fx_set_bg(FX_RGB(245,245,245));
    fx_set_color(FX_RGB(33,150,243));
    fx_fill_rect(40, 40, 240, 120);
    fx_draw_text_c(48, 56, "hello", FX_RGB(255,255,255), FX_RGB(33,150,243));
    fx_frame_end();
}
int main(void) {
    fx_init(&my_driver);          // 后端驱动（sokol/sdl/stub）
    fx_button_new(name("ok"), text("确定"), x(20), y(180), w(120), h(44));
    for (;;) {
        if (backend_should_quit()) break;
        fx_poll();                // 处理输入/布局/绘制（内部调 on_frame）
        backend_present();
    }
    return 0;
}
```

多窗口（FXTK_MW）主循环见 [multi-window.md](multi-window.md)；画布内窗口管理见 [window-manager.md](window-manager.md)。
---

## 23. 其余公开符号（构造宏 / 内部 helper / 多窗口注册）

| 符号 | 用法 |
|---|---|
| `fx_wptr(w)` | 属性 helper：把控件指针转成属性值（属性宏内部用；写自定义属性时用）。 |
| `fx_delete_impl(attrs[])` | `fx_delete(...)` 的底层实现：传属性数组删除匹配控件（一般直接用宏）。 |
| `fx_set_fgcolor_w(w,c)` | 旧名 helper：同 `fx_set_fgcolor(w,c)`（历史接口，保留兼容）。 |
| `fx_textedit_new(...)` | 构造宏：文本框控件（`fx_textedit_set_readonly/text` 配套）。 |
| `fx_scroll_new(...)` | 构造宏：滚动容器（`fx_scroll_content/fx_scroll_update` 配套）。 |
| `fx_image_new(...)` | 构造宏：图片控件（`image(img)` 属性绑定）。 |
| `fxtk_win_backend_set(b)` / `fxtk_win_backend_get()` | 注册/读取多窗口后端 vtable（平台外壳在 init 前调）。 |
| `fxtk_win_backend_name()` | 当前多窗口后端名（日志/诊断）。 |
| `fxtk_win_count()` | 已创建窗口数。 |
| `fxtk_win_destroy(id)` | 销毁指定窗口（通常只在退出/重建时用；关窗语义见 [multi-window.md](multi-window.md)）。 |
