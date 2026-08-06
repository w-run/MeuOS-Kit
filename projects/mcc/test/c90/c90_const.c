/* C90 const 关键字（§6.5.3，C90 引入 const） */

extern int puts(const char *);

int main(void)
{
    /* const 变量 */
    const int a = 100;
    if (a != 100) { puts("FAIL: const int"); return 1; }

    /* const 指针 */
    int x = 10;
    int y = 20;
    const int *p = &x;   /* 指向 const int 的指针 */
    if (*p != 10) { puts("FAIL: const ptr"); return 1; }

    p = &y;              /* 指针本身可修改 */
    if (*p != 20) { puts("FAIL: const ptr reassign"); return 1; }

    /* const 作为函数参数（保证不修改） */
    const int ca = 42;
    int b = ca;
    if (b != 42) { puts("FAIL: const param copy"); return 1; }

    /* const 数组 */
    const int arr[3] = { 1, 2, 3 };
    if (arr[0] != 1) { puts("FAIL: const arr[0]"); return 1; }
    if (arr[2] != 3) { puts("FAIL: const arr[2]"); return 1; }

    puts("PASS");
    return 0;
}