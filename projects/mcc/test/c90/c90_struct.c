/* C90 struct 基础（§6.5.2.1） */

extern int puts(const char *);

int main(void)
{
    /* 基本 struct 定义与使用 */
    struct point {
        int x;
        int y;
    };

    struct point p1;
    p1.x = 10;
    p1.y = 20;
    if (p1.x != 10) { puts("FAIL: struct p1.x"); return 1; }
    if (p1.y != 20) { puts("FAIL: struct p1.y"); return 1; }

    /* 初始化 */
    struct point p2 = { 30, 40 };
    if (p2.x != 30) { puts("FAIL: struct p2.x"); return 1; }
    if (p2.y != 40) { puts("FAIL: struct p2.y"); return 1; }

    /* 嵌套 struct */
    struct rect {
        struct point top_left;
        struct point bottom_right;
    };

    struct rect r = { { 0, 0 }, { 100, 200 } };
    if (r.top_left.x != 0)     { puts("FAIL: rect tl.x"); return 1; }
    if (r.bottom_right.y != 200) { puts("FAIL: rect br.y"); return 1; }

    /* struct 赋值 */
    struct point p3 = p2;
    if (p3.x != 30) { puts("FAIL: struct assign x"); return 1; }
    if (p3.y != 40) { puts("FAIL: struct assign y"); return 1; }

    /* sizeof struct */
    if (sizeof(struct point) != 2 * sizeof(int)) { puts("FAIL: sizeof struct"); return 1; }

    puts("PASS");
    return 0;
}