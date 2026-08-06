/* C90 综合兼容性测试：C90 下可用的各种类型组合 */

extern int puts(const char *);
extern int printf(const char *, ...);

/* 文件作用域 const */
const int GLOBAL_CONST = 100;

/* 枚举 */
enum status { OK = 0, ERROR = -1 };

/* 结构体 */
struct pair {
    int a;
    int b;
};

int main(void)
{
    /* const */
    if (GLOBAL_CONST != 100) { puts("FAIL: global const"); return 1; }

    /* 枚举 */
    enum status s = OK;
    if (s != 0) { puts("FAIL: enum status"); return 1; }

    /* struct 返回 */
    struct pair p = { 10, 20 };
    if (p.a != 10 || p.b != 20) { puts("FAIL: struct pair"); return 1; }

    /* volatile 声明（仅检查声明可通过） */
    volatile int v = 0;
    if (v != 0) { puts("FAIL: volatile init"); return 1; }

    /* unsigned */
    unsigned int ui = 0x80000000u;
    if (ui != 0x80000000u) { puts("FAIL: unsigned large"); return 1; }

    /* signed */
    int si = -1;
    int expected = -1;
    if (si != expected) { puts("FAIL: signed -1"); return 1; }

    puts("PASS");
    return 0;
}