/**
 * fxtk_tokens.h — v2.4 设计令牌 (design tokens)
 *
 * 为什么要有这个文件: 颜色/圆角/间距原先散落在 fxtk.c / fxtk_widgets.c 的几十处字面量里,
 * 想统一调一版"审美"就得满仓库改, 而且很容易漏掉某处导致同一种灰出现两三个近似值。
 * 现在所有视觉常量集中在这里 —— **改这一处就能统一换肤/统一圆角**, 也是 P5 控件审美优化的前提。
 *
 * 约定:
 *  - 名字语义化(TRACK / KNOB / ACCENT / MUTED…), 不写具体色号;
 *  - 控件代码只准引用令牌, 不准再写字面量(新控件照此办理);
 *  - 本次引入是**纯重构**: 所有令牌取值与改造前的字面量逐位相同, 因此金图回归必须仍然
 *    20/20 一致 —— 这就是"没有顺手改坏画面"的证据。
 */
#ifndef FXTK_TOKENS_H
#define FXTK_TOKENS_H

#include "fxtk.h"

/* ---------- 色板 ---------- */
#define FX_TOK_PRIMARY        FX_RGB(33, 150, 243)    /* 主色: 焦点环 / 选中 / 默认按钮 */
#define FX_TOK_SUCCESS        FX_RGB(76, 175, 80)     /* 成功/进度 */
#define FX_TOK_DANGER         FX_RGB(244, 67, 54)     /* 危险/删除 */
#define FX_TOK_WARN           FX_RGB(255, 152, 0)     /* 提醒 */
#define FX_TOK_TEXT           FX_RGB(40, 40, 40)      /* 正文 */
#define FX_TOK_TEXT_DIM       FX_RGB(120, 120, 120)   /* 次要文字/提示 */
#define FX_TOK_BORDER         FX_RGB(200, 200, 200)   /* 常规描边 */
#define FX_TOK_MUTED          FX_LGRAY                /* 静默底色 */
#define FX_TOK_ON_PRIMARY     FX_WHITE                /* 主色之上的文字 */

/* ---------- 控件专用 ---------- */
#define FX_TOK_TRACK          FX_RGB(200, 202, 206)   /* 滑杆轨道底色 */
#define FX_TOK_TRACK_EDGE     FX_RGB(140, 142, 148)   /* 轨道描边 */
#define FX_TOK_KNOB           FX_RGB(255, 255, 255)   /* 滑块面 */
#define FX_TOK_KNOB_EDGE      FX_RGB(150, 152, 158)   /* 滑块描边 */
#define FX_TOK_KNOB_EDGE_DOWN FX_RGB(90, 92, 98)      /* 滑块按下描边 */
#define FX_TOK_EDGE_DOWN      FX_RGB(60, 60, 60)      /* 按钮按下时的 1px 深边 */
#define FX_TOK_LIST_EDGE      FX_RGB(150, 150, 150)   /* 列表外框 */
#define FX_TOK_LIST_HOVER     FX_RGB(200, 220, 245)   /* 列表悬停行 */
#define FX_TOK_SCROLL_TRACK   FX_GRAY                 /* 滚动条槽 */
#define FX_TOK_SCROLL_THUMB   FX_LGRAY                /* 滚动条滑块 */
#define FX_TOK_SELECT_TEXT_BG FX_RGB(40, 40, 40)      /* 列表选中项文字底色 */

/* ---------- 几何 ---------- */
#define FX_TOK_RADIUS_S       4     /* 小圆角: 进度条/小按钮 */
#define FX_TOK_RADIUS_M       6     /* 中圆角: 常规按钮 */
#define FX_TOK_RADIUS_L       8     /* 大圆角: 卡片/面板 */
/* 按钮圆角: **保持保守(4)**。V2.4 试过加到 7 —— 单看一个按钮更"软", 但演示的键盘/网格是
 * 紧密相邻的格子, 每个格子四角一露就露出底色, 整片网格看着像"崩了"(用户实测反馈)。
 * 按钮常被成组紧密排列, 圆角必须保守; 需要更圆的场景(卡片/胶囊标签)另有令牌。 */
#define FX_TOK_RADIUS_BTN     4
#define FX_TOK_RADIUS_KNOB_DIV 3   /* 滑块圆角 = 宽度的 1/3(越大越"胶囊") */
#define FX_TOK_BTN_EDGE_MIX   25    /* 按钮描边加深比例(%): 提高与背景的分离度, 不增加绘制调用 */
#define FX_TOK_TRACK_H_MIN    6     /* 滑杆轨道厚度下限 */
#define FX_TOK_TRACK_H_MAX    10    /* 滑杆轨道厚度上限 */
#define FX_TOK_KNOB_W_MIN     12    /* 滑块宽度下限 */
#define FX_TOK_KNOB_W_MAX     20    /* 滑块宽度上限 */
#define FX_TOK_TEXT_PAD_X     6     /* 控件内文字左右留白 */

#endif /* FXTK_TOKENS_H */
