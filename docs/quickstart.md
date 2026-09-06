# fxtk 快速上手（PC / Linux 模拟器）

完整教程见 `docs/guide.md`；本页 5 分钟跑通。

## 构建与运行

在 `demo-main` 目录执行 `./build.sh`，自动编译并运行 `fxtk_sim`（完整 12 页演示，含 3D 光追/GPU 粒子）。窗口可任意拉伸甚至全屏，界面等比铺满。

```bash
cd demo-main
./build.sh                # 完整演示
./build_ex.sh ex03_anim   # 任意独立示例 ex01~ex18
```

依赖：`libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev`（`sudo apt install`）。

## 三步写一个界面

**1. 建控件**（不指定颜色也好看，默认浅底深字）：

```c
fx_label_new(percent("0.02,0.01","0.98,0.09"),
             title("我的应用"));                    /* 默认深字 */
fx_button_new(pixel("100,100","220,136"), name("btn"),
              title("点我"), call(on_click));       /* 默认蓝底白字 */
```

**2. 写回调**：

```c
static void on_click(fx_widget_t *w, void *ud) {
    static int n = 0;
    char b[32]; snprintf(b, sizeof b, "被点 %d 次", ++n);
    fx_set_title(w, b);          /* 自动重绘 */
}
```

**3. 画布自绘**（立即式）：

```c
static void on_draw(fx_widget_t *w, void *ud) {
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int cw=x2-x1+1, ch=y2-y1+1;
    fx_set_color(FX_RGB(30,30,30)); fx_fill_rect(0,0,cw-1,ch-1);
    fx_set_color(FX_YELLOW); fx_draw_circle(cw/2, ch/2, ch/3);
}
fx_canvas_new(pixel("6,32","444,236"), anim(1), call(on_draw));
```

## 布局选择

- 固定小控件 → `pixel()`（480×272 设计坐标，等比缩放）。
- 铺满/对齐父容器 → `percent()`（0.0~1.0）。
- 表格 → `grid()`。

## 常用操作

- 改文字 `fx_set_title`；改矩形 `fx_widget_set_rect`；
- 查控件 `fx_find("name")`；切换父 `fx_parent(...)`；
- 读鼠标 `fx_touch_state`；读按键 `fx_last_key`；
- 动态控件用 `fx_widget_fix` 固定坐标，数量上限由控件池（4096）决定。

## 全屏压测

演示的「压测」页在帧率富余（≥30fps）时**每帧自动增加控件**并让它们漂移，直到填满画布——全屏 1080P 下可容纳数千个。标题实时显示总控件数（`fxtk_widget_count()`）。

## 环境变量

| 变量 | 作用 |
|---|---|
| `FXTK_FONT=...` | 指定字体 |
| `FXTK_STAT=1` | 每秒打印渲染统计 |
| `FXTK_BENCH=1` | 关垂直同步测帧率 |
| `FXTK_MAXW=1920` | 缩放封顶 |
