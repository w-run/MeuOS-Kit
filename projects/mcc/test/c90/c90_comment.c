/* C90: 传统注释风格测试 */
/* 本文件全部使用 /star ... star/ 传统注释 */
/* 不含 // 风格的注释 */

extern int puts(const char *);

int main(void)
{
    int x = 0;

    /* 多行注释
       延续到下一行 */
    x = 42;

    if (x != 42) { puts("FAIL: comment value"); return 1; }

    puts("PASS");
    return 0;
}