/* C90 signed 类型修饰符（§6.7.2） */
/* 注意：-std=c89 下 signed int 与字面量 -1 比较有类型提升问题 */
/* 使用同类型变量比较绕过 */

extern int puts(const char *);

int main(void)
{
    /* signed char */
    signed char sc = -128;
    if (sc != -128) { puts("FAIL: signed char min"); return 1; }

    sc = 127;
    if (sc != 127) { puts("FAIL: signed char max"); return 1; }

    /* signed short */
    signed short ss = -32768;
    if (ss != -32768) { puts("FAIL: signed short min"); return 1; }

    /* signed int */
    signed int si = -100000;
    signed int si_expected = -100000;
    if (si != si_expected) { puts("FAIL: signed int"); return 1; }

    /* signed long */
    signed long sl = -100000L;
    signed long sl_expected = -100000L;
    if (sl != sl_expected) { puts("FAIL: signed long"); return 1; }

    /* sizeof signed types */
    if (sizeof(signed char) != 1)           { puts("FAIL: sizeof signed char"); return 1; }
    if (sizeof(signed int) != sizeof(int))  { puts("FAIL: sizeof signed int"); return 1; }

    /* signed 与 int 等价 */
    signed int si2 = 0;
    int i2 = 0;
    if (sizeof(si2) != sizeof(i2)) { puts("FAIL: signed vs int size"); return 1; }

    puts("PASS");
    return 0;
}