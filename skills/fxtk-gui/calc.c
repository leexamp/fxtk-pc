/* è®¡ç®å¨ ââ æ°æ®é©±å¨åæ³ï¼ä¸ä¸ªè¡¨ + ä¸ä¸ªåè° + ä¸ä¸ªæ±å¼å½æ° */
#include "fxtk.h"
#include "fxtk_desktop.h"
#include "fxtk_app.h"
#include <stdio.h>
#include <stdlib.h>

static fx_widget_t *s_disp = 0;
static double s_acc = 0;   /* ç´¯å å¨ */
static char   s_op  = 0;   /* å¾æ§è¡è¿ç®ç¬¦ */
static int    s_fresh = 1; /* ä¸ä¸ä¸ªæ°å­æ¯å¦è¦è¦çæ¾ç¤º */

static const char *KEYS[5][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", ".", "=", "+" },
    { "C", "",  "",  ""  },
};

static double disp_val(void)
{
    const char *t = fx_textedit_text(s_disp);
    return t ? atof(t) : 0.0;
}

static void show(const char *s) { if (s_disp) fx_set_title(s_disp, s); }

static void on_key(fx_widget_t *w, void *ud)
{
    (void)ud;
    const char *k = fx_widget_title(w);
    if (!k || !k[0]) return;
    char buf[32];

    if ((k[0] >= '0' && k[0] <= '9') || k[0] == '.') {          /* æ°å­/å°æ°ç¹ */
        if (s_fresh) { show(k); s_fresh = 0; }
        else {
            snprintf(buf, sizeof buf, "%s%s", fx_textedit_text(s_disp), k);
            show(buf);
        }
        return;
    }
    if (k[0] == 'C') { s_acc = 0; s_op = 0; s_fresh = 1; show("0"); return; }

    double cur = disp_val();
    if (k[0] == '=') {                                          /* æ±å¼ */
        if (s_op) {
            if (s_op == '+') s_acc += cur;
            else if (s_op == '-') s_acc -= cur;
            else if (s_op == '*') s_acc *= cur;
            else if (s_op == '/' && cur != 0) s_acc /= cur;
            s_op = 0;
        } else s_acc = cur;
        snprintf(buf, sizeof buf, "%g", s_acc);
        show(buf);
        s_fresh = 1;
        return;
    }
    /* ååè¿ç®ç¬¦ */
    if (s_op) {
        if (s_op == '+') s_acc += cur;
        else if (s_op == '-') s_acc -= cur;
        else if (s_op == '*') s_acc *= cur;
        else if (s_op == '/' && cur != 0) s_acc /= cur;
    } else s_acc = cur;
    s_op = k[0];
    s_fresh = 1;
}

void fxtk_app_init(void)
{
    fx_set_bg(FX_RGB(240, 240, 244));

    s_disp = fx_textedit_new(pixel("10,8", "470,58"), name("disp"), title("0"));
#ifdef FXTK_HAVE_READONLY   /* 仅 v2.4.5+ 有此实现；旧树上跳过（verify.sh 会自动探测） */
    fx_textedit_set_readonly(s_disp, 1);
#endif

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            if (!KEYS[r][c][0]) continue;
            char rect[16], rect2[16];
            int x1 = 10 + c * 116, y1 = 68 + r * 40;
            snprintf(rect,  sizeof rect,  "%d,%d", x1, y1);
            snprintf(rect2, sizeof rect2, "%d,%d", x1 + 108, y1 + 34);
            fx_button_new(pixel(rect, rect2), title(KEYS[r][c]), call(on_key));
        }
    }
}
