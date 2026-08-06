/* C90: 声明必须在语句之前（§6.8.2，C90 要求所有声明在块开头） */
/* 本测试所有声明都在块开头，符合 C90 规范 */

extern int puts(const char *);

int main(void)
{
    int x = 10;
    int y = 20;
    int z;

    /* 所有声明都在前面，下面是纯语句 */
    z = x + y;
    if (z != 30) { puts("FAIL: decl before stmt"); return 1; }

    /* 复合语句块，声明也在开头 */
    {
        int a = 1;
        int b = 2;
        int c = a + b;
        if (c != 3) { puts("FAIL: inner block decl"); return 1; }
    }

    puts("PASS");
    return 0;
}