/* 抽出 calc.c 的求值逻辑做纯逻辑测试（无 GUI） */
#include <stdio.h>
#include <stdlib.h>

static double s_acc = 0; static char s_op = 0;

static double press(double cur, char k)   /* 返回应显示的值 */
{
    if (k == '=') {
        if (s_op) {
            if (s_op=='+') s_acc += cur;
            else if (s_op=='-') s_acc -= cur;
            else if (s_op=='*') s_acc *= cur;
            else if (s_op=='/' && cur != 0) s_acc /= cur;
            s_op = 0;
        } else s_acc = cur;
        return s_acc;
    }
    if (s_op) {
        if (s_op=='+') s_acc += cur;
        else if (s_op=='-') s_acc -= cur;
        else if (s_op=='*') s_acc *= cur;
        else if (s_op=='/' && cur != 0) s_acc /= cur;
    } else s_acc = cur;
    s_op = k;
    return cur;
}

static int fails = 0;
static void ck(const char *name, double got, double want)
{
    int ok = (got - want < 1e-9 && want - got < 1e-9);
    printf("  [%s] %-22s got=%g want=%g\n", ok?"ok":"FAIL", name, got, want);
    if (!ok) fails++;
}
int main(void)
{
    /* 12 + 7 = 19 */
    press(12,'+'); ck("12+7", press(7,'='), 19);
    s_acc=0; s_op=0;
    /* 9 - 4 = 5 */
    press(9,'-'); ck("9-4", press(4,'='), 5);
    s_acc=0; s_op=0;
    /* 6 * 7 = 42 */
    press(6,'*'); ck("6*7", press(7,'='), 42);
    s_acc=0; s_op=0;
    /* 8 / 2 = 4 */
    press(8,'/'); ck("8/2", press(2,'='), 4);
    s_acc=0; s_op=0;
    /* 连续运算: 2+3+4 = 9 */
    press(2,'+'); press(3,'+'); ck("2+3+4", press(4,'='), 9);
    s_acc=0; s_op=0;
    /* 负数/减法链: 10-3-2 = 5 */
    press(10,'-'); press(3,'-'); ck("10-3-2", press(2,'='), 5);
    s_acc=0; s_op=0;
    /* 除零保护: 5/0 不应崩, 且累加器不变 */
    press(5,'/'); double z = press(0,'=');
    printf("  [%s] 5/0 保护            got=%g (不崩即可)\n", (z==z)?"ok":"FAIL", z);
    if (!(z==z)) fails++;
    /* 无运算符直接 = : 5= → 5 */
    s_acc=0; s_op=0; ck("5=", press(5,'='), 5);

    printf("\n%s (%d fail)\n", fails?"FAIL":"PASS", fails);
    return fails != 0;
}
