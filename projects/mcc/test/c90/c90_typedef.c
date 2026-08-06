/* C90 typedef 基础 */

extern int puts(const char *);

int main(void)
{
    typedef int myint;
    myint x = 42;
    if (x != 42) { puts("FAIL: typedef int"); return 1; }

    typedef struct { int x; int y; } point_t;
    point_t p;
    p.x = 10;
    p.y = 20;
    if (p.x != 10) { puts("FAIL: typedef struct x"); return 1; }
    if (p.y != 20) { puts("FAIL: typedef struct y"); return 1; }

    typedef unsigned long ulong;
    ulong u = 100UL;
    if (u != 100UL) { puts("FAIL: typedef ulong"); return 1; }

    /* 函数指针 typedef */
    typedef int (*op_fn)(int, int);
    /* 仅验证 typedef 语法正确 */

    puts("PASS");
    return 0;
}