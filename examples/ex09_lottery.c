/** ex09_lottery — 抽奖: 名单池 + 滚动抽取 + 中奖填格 */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE 12
#define MIN_LINE 4
#define MAX_WIN  9

/* 常见中英文名单池 */
static const char *s_pool[] = {
    "张伟", "王芳", "李娜", "刘洋", "陈静", "杨帆",
    "赵磊", "黄敏", "周涛", "吴婷", "徐强", "孙丽",
    "Alice", "Bob", "Charlie", "Diana", "Eric", "Fiona",
};
#define POOL_N ((int)(sizeof(s_pool)/sizeof(s_pool[0])))

static int num_line = 4;
static fx_widget_t *s_wins[MAX_WIN];
static int s_win_n = 0;
static int s_pick[MAX_WIN];
static int s_roll = 0, s_roll_t = 0;

static void build_table(void);

static void on_minus(fx_widget_t *w, void *ud) { if (!s_roll && num_line > MIN_LINE) { num_line--; build_table(); } }
static void on_add(fx_widget_t *w, void *ud)   { if (!s_roll && num_line < MAX_LINE) { num_line++; build_table(); } }

/* 洗牌式不重复抽取 */
static void pick_final(void)
{
    int idx[POOL_N];
    for (int i = 0; i < POOL_N; i++) idx[i] = i;
    for (int i = POOL_N - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (int i = 0; i < s_win_n; i++) s_pick[i] = idx[i];
}

static void on_guess(fx_widget_t *w, void *ud)
{
    if (s_roll) return;
    pick_final();
    s_roll = 1; s_roll_t = 0;
    fx_set_title(fx_find("btn_go"), "抽取中…");
}

/* 隐藏 anim 画布: 滚动动画驱动器 */
static void on_tick(fx_widget_t *w, void *ud)
{
    if (!s_roll) return;
    s_roll_t++;
    if (s_roll_t % 5 == 0)   /* 名字滚动闪烁 */
        for (int i = 0; i < s_win_n; i++)
            fx_set_title(s_wins[i], s_pool[rand() % POOL_N]);
    if (s_roll_t >= 100) {   /* ~1.7s 后定格 */
        s_roll = 0;
        for (int i = 0; i < s_win_n; i++)
            fx_set_title(s_wins[i], s_pool[s_pick[i]]);
        fx_set_title(fx_find("btn_go"), "开始抽奖");
    }
}

static void build_table(void)
{
    const char *ids[] = { "btn_minus","btn_plus","btn_go","lbl_head","lbl_num","main_table" };
    for (int i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
        fx_widget_t *w = fx_find(ids[i]);
        if (w) fx_delete(fx_wptr(w));
    }
    for (int i = 0; i < MAX_WIN; i++) {
        char wn[16]; snprintf(wn, sizeof(wn), "win%d", i);
        fx_widget_t *w = fx_find(wn);
        if (w) fx_delete(fx_wptr(w));
    }

    fx_grid_map(percent("0,0","1,1"), line(num_line), row(3),
                name("main_table"), color(FX_RGB(240,240,240)), fgcolor(FX_RGB(200,200,200)));
    fx_label_new(grid("main_table",1,1,1,3), name("lbl_head"),
                 title("恭喜以下人员中奖了："), fgcolor(FX_RGB(40,40,40)));

    /* 中奖槽: 第 2 .. num_line-2 行 */
    s_win_n = num_line - 3;
    for (int i = 0; i < s_win_n; i++) {
        char nm[16]; snprintf(nm, sizeof(nm), "win%d", i);
        s_wins[i] = fx_label_new(grid("main_table", 2+i, 1, 2+i, 3),
                                 name(nm), title("——"), fgcolor(FX_RGB(33, 100, 200)));
    }

    fx_button_new(grid("main_table",num_line-1,1,num_line-1,1), name("btn_minus"),
                  title("-"), color(FX_RGB(33,150,243)), fgcolor(FX_WHITE), call(on_minus));
    char num_str[16]; snprintf(num_str, sizeof(num_str), "%d", s_win_n);
    fx_widget_t *nl = fx_label_new(grid("main_table",num_line-1,2,num_line-1,2),
                                   name("lbl_num"), title(num_str), fgcolor(FX_RGB(40,40,40)));
    {   /* 数字居中 */
        int x1,y1,x2,y2; fx_widget_rect(nl,&x1,&y1,&x2,&y2);
        int tw = fx_text_width(num_str), th = 20;
        int nx1 = x1+((x2-x1+1)-tw)/2, ny1 = y1+((y2-y1+1)-th)/2;
        fx_widget_set_rect(nl, nx1, ny1, nx1+tw-1, ny1+th-1);
    }
    fx_button_new(grid("main_table",num_line-1,3,num_line-1,3), name("btn_plus"),
                  title("+"), color(FX_RGB(33,150,243)), fgcolor(FX_WHITE), call(on_add));
    fx_button_new(grid("main_table",num_line,1,num_line,3), name("btn_go"),
                  title("开始抽奖"), color(FX_RGB(76,175,80)), call(on_guess));
}

void app_init(void)
{
    srand((unsigned)time(NULL));
    fx_set_bg(FX_RGB(240,240,240));
    fx_set_window_title("抽奖");
    if (getenv("FXTK_DEBUG")) fx_set_grid_lines(1);
    /* 隐藏小画布 = 动画驱动器 (先建, 被 grid 盖住) */
    fx_canvas_new(pixel("474,268","477,271"), name("ticker"), anim(1),
                  color(FX_RGB(240,240,240)), call(on_tick));
    build_table();
}
