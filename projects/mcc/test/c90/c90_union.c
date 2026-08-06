/* C90 union 基础（§6.5.2.3） */

extern int puts(const char *);

int main(void)
{
    union data {
        int i;
        unsigned int u;
        float f;
    };

    union data d;

    /* 写入 int，读取 int */
    d.i = -42;
    if (d.i != -42) { puts("FAIL: union int"); return 1; }

    /* 写入 unsigned，读取 unsigned */
    d.u = 0xFFFFFFFFu;
    if (d.u != 0xFFFFFFFFu) { puts("FAIL: union unsigned"); return 1; }

    /* 大小验证 */
    if (sizeof(union data) < sizeof(int))    { puts("FAIL: union size < int"); return 1; }
    if (sizeof(union data) < sizeof(float))  { puts("FAIL: union size < float"); return 1; }

    /* 初始化 */
    union data d2 = { 123 };
    if (d2.i != 123) { puts("FAIL: union init"); return 1; }

    puts("PASS");
    return 0;
}