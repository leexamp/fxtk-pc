---
name: fxtk-gui
description: Write a small GUI program in C with the fxtk framework (single-file binary, no third-party DLLs). Use when asked for a window, buttons, sliders, text box, canvas drawing, animation, or a small desktop tool in C.
---

# fxtk — 给小模型写 GUI

## 给受限模型的操作建议（先读这段）

实测：**一个能跑的计算器约 900 token**。在 6 tok/s 的本地模型上约 **2.5 分钟**。
所以卡住你的通常不是生成速度，而是**上下文**：

1. **默认只喂 `TOKENS.md`（约 1200 token）**。不要一次喂 `REFERENCE.md` + `ref_app.c`
   （合计约 4400 token）—— 那会塞满小上下文窗口，模型还没开始写就没地方了。
2. 要细节时**按需读一页**，不要全量喂。
3. **别让模型从头写**：让它抄 `calc.c` 或 `ref_app.c`，**只改要改的那几行**。
   改 20 行 ≈ 60 token ≈ 10 秒；重新发明一遍 ≈ 900 token ≈ 2.5 分钟，还容易编错 API。

## 文件

| 文件 | 大小 | 什么时候用 |
|---|---|---|
| `TOKENS.md` | ~1200 token | **默认喂这个**。API 速查卡，够写绝大多数小程序 |
| `calc.c` | ~930 token | 完整可运行的计算器（数据驱动 + 求值逻辑，逻辑已单测通过） |
| `ref_app.c` | ~1400 token | 参考实现：每种控件的标准用法 |
| `REFERENCE.md` | ~3200 token | 完整文档 + 陷阱表 + 属性宏全清单。**按需查，别全量喂** |
| `calc_logic_test.c` | ~400 token | 计算器求值逻辑的纯逻辑单元测试（无 GUI），可独立编译 |
| `verify.sh` | — | 编译 `ref_app.c` 并检查 0 warning，防止文档与代码脱节 |

## 最快的路子

```bash
cd your_app
cp ../skills/fxtk-gui/calc.c main.c     # 或 ref_app.c
make && ./your_app
```

跑起来之后，让模型**只改需要改的地方**。

## 硬规则（不遵守必定编译失败）

1. 入口是 `void fxtk_app_init(void)`。**不要写 `main()`** —— 外壳提供。
2. 位置是 `pixel("x1,y1","x2,y2")` —— **两个字符串**，不是四个数字。
3. `fx_textedit_new` / `fx_scroll_new` / `maxlen` 在 `fxtk_desktop.h`，**不在 `fxtk.h`**。
4. 只读文本框用 `fx_textedit_set_readonly`，**仅 v2.4.5+ 存在**。
5. 不要用 `fxtk_font_height`（有声明无定义，链接必失败）。
6. 改控件（**含文本框内容**）用 `fx_set_title`，读文本框用 `fx_textedit_text`；
   改完自动重绘，**不要调 `fx_repaint`**。⚠ 改文本框**仅 v2.4.5+ 生效**
   （更早版本是空操作，症状是"按钮点了但输入框不变"）。

细节与完整陷阱表 → `REFERENCE.md`
