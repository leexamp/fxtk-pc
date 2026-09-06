/**
 * ex07_snake — 贪吃蛇 (完整注释学习版)
 *
 * ★ 知识点 1: canvas vs grid 怎么选?
 *   - grid  = "布局容器": 把矩形切格子里【摆控件】(按钮/标签), 适合做 UI。
 *     本例的方向键盘(D-pad)用它, 4 个按钮自动排齐。
 *   - canvas = "绘图画板": anim(1) 后回调【每帧执行】, 立即模式画几百个矩形
 *     毫无压力。游戏场地/动画/图表必须用它。
 *   反例: 若把蛇的每个格子做成控件, 24x16=384 个控件, 烧控件池且重绘慢。
 *
 * ★ 知识点 2: 游戏主循环在哪?
 *   fxtk 没有独立定时器; anim(1) 画布的回调就是你的 game loop (每帧调用)。
 *   用 s_tick 帧计数 % interval 控制"蛇每几步移动一次" = 速度。
 *
 * ★ 知识点 3: 游戏键盘不需要焦点
 *   fx_last_key() 是全局 API, 任何键都收得到 (文本框焦点路由不影响它)。
 *   用 memcmp 与上一帧比较实现"一次按键只响应一次" (边沿触发)。
 *
 * ★ 知识点 4: 控件做 UI, 画布做画面
 *   分数 = fx_set_title() 改标签; D-pad 按钮 = fx_set_cb() 的 ud 传"方向编号"。
 *
 * 操作: WASD / 左右方向键 / 屏幕 D-pad; 空格=开始/暂停/重来
 */
#include "fxtk.h"          /* 核心: 控件/画布/绘图原语 */
#include "fxtk_desktop.h"  /* 桌面扩展声明 (fx_last_key 等) */
#include <stdio.h>         /* snprintf */
#include <stdlib.h>        /* rand */
#include <string.h>        /* memcmp */

/*================ 配置常量 ================*/
#define GW 24              /* 场地列数 (格子) */
#define GH 16              /* 场地行数 */
#define MAXS 400           /* 蛇身数组上限, 防越界 */

/* 一个格子的坐标: 范围 -128~127, int8_t 足够且省内存 */
typedef struct { int8_t x, y; } pt_t;

/*================ 游戏状态 (demo 用静态全局, 简单直接) ================*/
static pt_t s_body[MAXS];  /* 蛇身, [0] 永远是头 */
static int s_len;          /* 当前长度 */
static int s_dx, s_dy;     /* 这一步【实际】移动方向 */
static int s_ndx, s_ndy;   /* 缓冲的新方向: 一步内连按两键也不会 180° 掉头 */
static int s_fx, s_fy;     /* 食物坐标 */
static int s_score;        /* 得分 */
static int s_over;         /* 1=结束 */
static int s_run;          /* 1=进行中 */
static int s_tick;         /* 帧计数 (控速用) */
static fx_keyev_t s_consumed; /* 上一帧已消费的按键 (去重=边沿触发) */

/*---------------- 放食物: 随机但不能落在蛇身上 ----------------*/
static void place_food(void)
{
    while (1) {
        s_fx = rand() % GW;
        s_fy = rand() % GH;
        int hit = 0;
        for (int i = 0; i < s_len; i++)
            if (s_body[i].x == s_fx && s_body[i].y == s_fy) { hit = 1; break; }
        if (!hit) return;          /* 不冲突就用这个位置 */
    }
}

/*---------------- 重置一局 ----------------*/
static void reset(int run)
{
    s_len = 3;                     /* 初始 3 节, 头在右 */
    s_body[0] = (pt_t){5, 8};
    s_body[1] = (pt_t){4, 8};
    s_body[2] = (pt_t){3, 8};
    s_dx = s_ndx = 1; s_dy = s_ndy = 0;   /* 初始向右 */
    s_score = 0; s_over = 0; s_run = run;
    place_food();
    char b[32]; snprintf(b, sizeof(b), "得分: %d", s_score);
    fx_set_title(fx_find("score"), b);    /* 标签刷新: 控件做 UI */
}

/*---------------- 转向: 禁止 180° 掉头 (会撞自己脖子) ----------------*/
static void set_dir(int dx, int dy)
{
    if (dx == -s_dx && dy == -s_dy) return;
    s_ndx = dx; s_ndy = dy;        /* 只缓冲, 下一步 step() 才生效 */
}

/*---------------- D-pad 按钮回调: ud = 方向编号 0上1下2左3右 ----------------*/
static void on_pad(fx_widget_t *w, void *ud)
{
    int d = (int)(intptr_t)ud;     /* ud 是 void*, 转回整数 */
    if (!s_run && !s_over) reset(1);          /* 未开始时按方向=直接开局 */
    if      (d == 0) set_dir(0, -1);
    else if (d == 1) set_dir(0,  1);
    else if (d == 2) set_dir(-1, 0);
    else             set_dir(1,  0);
}

/*---------------- 开始/暂停/重来 ----------------*/
static void on_start(fx_widget_t *w, void *ud)
{
    if (s_over) reset(1);          /* 结束后按=重开 */
    else s_run = !s_run;           /* 否则切换暂停 */
}

/*---------------- 键盘轮询 (在画布回调里每帧调用) ----------------*/
static void poll_keys(void)
{
    fx_keyev_t k = fx_last_key();  /* 全局最后按键, 无需焦点 */
    /* 与上帧相同 => 不是新按键, 直接返回 (按住不重复触发) */
    if (memcmp(&k, &s_consumed, sizeof(k)) == 0) return;
    s_consumed = k;

    if      (k.key == FX_KEY_LEFT)  set_dir(-1, 0);
    else if (k.key == FX_KEY_RIGHT) set_dir( 1, 0);
    /* 上下用 W/S: 核心键表里 LEFT/RIGHT 现成, UP/DOWN 走字符通道 */
    else if (k.utf8[0]=='w'||k.utf8[0]=='W') set_dir(0,-1);
    else if (k.utf8[0]=='s'||k.utf8[0]=='S') set_dir(0, 1);
    else if (k.utf8[0]=='a'||k.utf8[0]=='A') set_dir(-1,0);
    else if (k.utf8[0]=='d'||k.utf8[0]=='D') set_dir( 1,0);
    else if (k.utf8[0]==' '||k.key==FX_KEY_RETURN) on_start(NULL, NULL);
}

/*---------------- 逻辑一步: 移动 + 撞墙 + 撞自己 + 吃食物 ----------------*/
static void step(void)
{
    s_dx = s_ndx; s_dy = s_ndy;    /* 缓冲方向转正 */
    pt_t h = { (int8_t)(s_body[0].x + s_dx), (int8_t)(s_body[0].y + s_dy) };

    /* 撞墙 */
    if (h.x < 0 || h.y < 0 || h.x >= GW || h.y >= GH) { s_over = 1; return; }
    /* 撞自己 (严格说尾格下一步会让开, demo 从简全判) */
    for (int i = 0; i < s_len; i++)
        if (s_body[i].x == h.x && s_body[i].y == h.y) { s_over = 1; return; }

    int eat = (h.x == s_fx && h.y == s_fy);
    if (eat) {                     /* 吃到: 加分+变长+换食物+刷标签 */
        s_score++;
        if (s_len < MAXS) s_len++;
        place_food();
        char b[32]; snprintf(b, sizeof(b), "得分: %d", s_score);
        fx_set_title(fx_find("score"), b);
    }
    /* 身体整体后移一格 (从尾往前拷), 头放到 [0]。
       没吃到时长度不变 => 尾巴自然"缩"一格, 等效移动 */
    for (int i = s_len - 1; i > 0; i--) s_body[i] = s_body[i - 1];
    s_body[0] = h;
}

/*---------------- 画布回调 = 游戏主循环 (每帧执行) ----------------*/
static void on_snake(fx_widget_t *w, void *ud)
{
    /* 1) 取画布实际像素尺寸 (窗口拉伸时也正确) */
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;

    /* 2) 算格子像素边长 (取宽/高两个方向较小者, 保证正方形) 与居中偏移 */
    int cs = (cw / GW < ch / GH) ? cw / GW : ch / GH;
    int ox = (cw - cs * GW) / 2, oy = (ch - cs * GH) / 2;

    /* 3) 输入 (每帧都问, 边沿触发在 poll_keys 内部保证) */
    poll_keys();

    /* 4) 逻辑 tick: interval 帧走一步; 吃得越多 interval 越小 = 越快 */
    s_tick++;
    int bonus = s_score / 4; if (bonus > 8) bonus = 8;
    int interval = 12 - bonus;
    if (s_run && !s_over && (s_tick % interval) == 0) step();

    /* 5) 绘制: 先铺底色 */
    fx_set_color(FX_RGB(20, 20, 26)); fx_fill_rect(0, 0, cw - 1, ch - 1);

    /* 食物 (红) */
    fx_set_color(FX_RGB(244, 67, 54));
    fx_fill_rect(ox + s_fx * cs + 1, oy + s_fy * cs + 1,
                 ox + (s_fx + 1) * cs - 2, oy + (s_fy + 1) * cs - 2);

    /* 蛇: 头用亮色, 身用普通绿 (+1/-2 留出格缝, 看出一节一节) */
    for (int i = 0; i < s_len; i++) {
        fx_set_color(i == 0 ? FX_RGB(178, 255, 89) : FX_RGB(76, 175, 80));
        fx_fill_rect(ox + s_body[i].x * cs + 1, oy + s_body[i].y * cs + 1,
                     ox + (s_body[i].x + 1) * cs - 2, oy + (s_body[i].y + 1) * cs - 2);
    }

    /* 提示文字 (居中): fx_text_width 量宽度才能真居中 */
    const char *msg = s_over ? "游戏结束! 空格重来"
                    : (!s_run ? "空格/点击 开始" : NULL);
    if (msg) {
        int tw = fx_text_width(msg);
        fx_draw_text_c((cw - tw) / 2, ch / 2 - 8, msg,
                       FX_YELLOW, FX_RGB(20, 20, 26));
    }
}

/*================ UI 搭建 ================*/
void app_init(void)
{
    fx_set_bg(FX_RGB(240, 240, 240));

    /* 标题 + 分数: 普通标签 (percent 自适应 / pixel 固定) */
    fx_label_new(percent("0.03,0.03", "0.55,0.12"),
                 title("fxtk 示例 07 — 贪吃蛇"), fgcolor(FX_RGB(40, 40, 40)));
    fx_label_new(pixel("300,10", "470,32"), name("score"),
                 title("得分: 0"), fgcolor(FX_RGB(40, 40, 40)));

    /* ★ 游戏场地: canvas + anim(1) => on_snake 每帧回调 */
    fx_canvas_new(pixel("10,40", "330,262"), name("field"), anim(1),
                  color(FX_RGB(20, 20, 26)), call(on_snake));

    /* ★ grid 的正确用法: 3x3 格子里摆方向键 (只负责排按钮, 不画游戏) */
    fx_grid_map(pixel("345,60", "465,180"), line(3), row(3), name("pad"));
    fx_button_new(grid("pad",1,2,1,2), name("pu"), title("↑"), color(FX_RGB(33,150,243)), call(on_pad));
    fx_button_new(grid("pad",2,1,2,1), name("pl"), title("←"), color(FX_RGB(33,150,243)), call(on_pad));
    fx_button_new(grid("pad",2,3,2,3), name("pr"), title("→"), color(FX_RGB(33,150,243)), call(on_pad));
    fx_button_new(grid("pad",3,2,3,2), name("pd"), title("↓"), color(FX_RGB(33,150,243)), call(on_pad));
    /* call() 只设了回调, ud 要用 fx_set_cb 补: 传方向编号 */
    fx_set_cb(fx_find("pu"), on_pad, (void*)(intptr_t)0);
    fx_set_cb(fx_find("pd"), on_pad, (void*)(intptr_t)1);
    fx_set_cb(fx_find("pl"), on_pad, (void*)(intptr_t)2);
    fx_set_cb(fx_find("pr"), on_pad, (void*)(intptr_t)3);

    fx_button_new(pixel("345,200", "465,235"), title("开始/暂停"),
                  color(FX_RGB(76, 175, 80)), call(on_start));
    fx_label_new(pixel("345,240", "470,262"), title("WASD/方向键/D-pad"),
                 fgcolor(FX_RGB(120, 120, 120)));

    reset(0);   /* 摆好初始局面, 等玩家开始 */
}
