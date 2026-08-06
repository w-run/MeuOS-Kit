/* C90 volatile 关键字（§6.5.3，C90 引入 volatile） */
/* 注意：mcc 暂不支持 volatile store（E0009），仅测试声明和读取 */

extern int puts(const char *);

int main(void)
{
    /* volatile 变量（初始化） */
    volatile int flag = 0;
    if (flag != 0) { puts("FAIL: volatile init"); return 1; }

    /* volatile 指针 */
    int x = 42;
    volatile int *vp = &x;
    if (*vp != 42) { puts("FAIL: volatile ptr"); return 1; }

    /* const volatile 组合 */
    const volatile int cv = 100;
    int r = cv;
    if (r != 100) { puts("FAIL: const volatile"); return 1; }

    /* volatile 不影响 sizeof */
    if (sizeof(volatile int) != sizeof(int)) { puts("FAIL: sizeof volatile"); return 1; }

    puts("PASS");
    return 0;
}