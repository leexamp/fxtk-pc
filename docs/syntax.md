# 语法与边界规则（属性 / 回调 / 布局 / 命名）

> 这份文档回答"属性宏写法在边界情况下到底怎么算"，避免靠猜。实现见 `components/fxtk/fxtk.c`
> 的 `fx_widget_new_impl()` / `fx_layout()` / `fx_touch_release()`。

## 1. 属性应用顺序与冲突

- 构造宏是变参属性列表：`fx_button_new(A, B, C)` 按 **从左到右** 依次应用 A→B→C。
- **同一字段后写覆盖先写**。例：`fx_label_new(title("A"), title("B"))` 最终标题是 B；
  `color(red), color(blue)` 最终蓝色。
- 位置属性有优先级：`pixel / percent / grid` 都写同一个 `pos_mode` 字段 —— **最后出现的那个决定布局方式**，
  不是"pixel 优先"。混合写属于未定义风格，建议一个控件只用一种定位方式。
- 同名字段不存在"合并"：例如 `pixel("10,10","100,40")` 之后再写 `percent(...)`，
  `pos_mode` 会变成 PERCENT、坐标按百分比重新解释（两个都写属于未定义风格）。

## 2. call() 回调生命周期

- `call(cb)` 把 `cb` 与 `ud` 存到控件对象里；触发点：按钮/复选框（点击抬起）、滑杆（拖动值变化）、
  标签页（换页）、列表/下拉（选中行）、文本编辑（内容变化时由控件实现决定）。
- 回调在 `fx_poll()` / 触摸处理栈内执行，**不要在回调里删除自己或祖先控件**（会破坏正在遍历的链表/焦点状态）。
  需要删除时：置一个"待删除"标记，下一帧在 `fx_poll()` 之外用 `fx_delete`。
- 控件删除后：`cb/ud` 随对象槽位清零，不会再被调用；但**外部保存的 `fx_widget_t*` 立即失效**
  （池地址会被复用）。删除时框架会清理 `pressed/focus/scroll` 与 `extra/list/动画`槽位（v2.5.1）。
- `ud` 的生命周期由调用者负责；框架只透传，不释放。

## 3. 布局优先级：percent / grid / pixel

- `pixel`：**设计稿坐标，不是设备像素**。默认设计稿 480×272；换成自己的稿子就调
  `fx_set_design_pixel(w,h)`（例如 1280×720），之后 `pixel("980,600","1200,680")` 直接按该稿量。
  映射到当前画布时按 `s_sx/s_sy` 比例、宽高分别缩放 —— 非 16:9 窗口横纵比例会不同
  （用默认稿时 demo 窗口请取 480×272 的整数倍附近，如 640×360）。
- `percent`：相对**父控件矩形**的百分比（"0.25,0.25" 表示父宽的 25% 处）。
- `grid`：引用一个命名网格控件（`fx_grid_map(... name("g"))`），按行列切片定位；
  网格本身再被 `pixel/percent` 定位 —— 即 grid 是"两级"的。
- **嵌套时**：子控件先按自己的 pos_mode 算，再叠加父控件原点；父控件若后于子控件改变，下一帧 `fx_layout()` 生效。
- `fx_widget_fix(w,x,y)` 会跳过自动布局（固定坐标），适合动态创建控件后防止被 `fx_layout()` 拉回设计坐标。

## 4. 命名与查找

- `fx_find("id")` 在**整个控件池线性扫描**，返回**第一个**匹配项。
- 名字**全局唯一**是使用约定（不是强制检查）：重名时 `fx_find` 行为是"找到哪个算哪个"，
  多窗口/多页面下更要用前缀（如 `mw1_btn` / `mw2_btn`）。
- 匿名控件（不写 `name()`）无法被 `fx_find/fx_delete` 按名定位，只能通过保存的指针操作（注意指针失效规则）。

## 5. 宏展开与常见编译错误

- 属性宏产生的是 C99 复合字面量 `(fx_attr_t[]){ ... , FX_ATTR_END }`；**列表最后不需要手写 END**（构造宏会补）。
- 常见错误：
  - `error: expected ')' before 'name'` —— 混入了非属性表达式（把语句写进属性列表）；
  - `error: initializer element is not constant` —— 在全局/静态初始化里用构造宏（必须放函数内）；
  - `implicit declaration of fx_xxx_new` —— 忘记 `#include "fxtk.h"`（桌面控件还要 `fxtk_desktop.h`）；
  - `invalid use of incomplete typedef` —— 应用层拿 `fx_widget_t*` 直接访问字段；用 API 读取（`fx_get_value` /
    `fx_widget_title` / `fx_widget_rect`）。
- 属性函数名（`pixel/percent/grid/title/name/color/call/line/row/anim...`）是全局函数；
  自己的符号请避开这些短名（尤其 `name`）。

## 6. 多窗口相关（v2.5.2）

- 多窗口时每扇窗一棵根：`fx_root_new()` + `fx_root_select(r)`；`fx_find` 仍是全池查找，
  因此跨窗口的名字要带前缀。
- 焦点/按压/右键菜单属于"当前活动根"：切换根后，属于旧根的键盘输入会被忽略（不会串窗）；
  需要跨窗保持焦点时，把焦点控件放在对应根内并在该窗口被点击时重新 `fx_set_focus(w)`。

