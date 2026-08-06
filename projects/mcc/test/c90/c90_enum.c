/* C90 enum 基础（§6.5.2.2，C90 引入枚举类型） */

extern int puts(const char *);

int main(void)
{
    enum color { RED, GREEN, BLUE };
    enum color c;

    c = RED;
    if (c != 0) { puts("FAIL: enum RED"); return 1; }

    c = GREEN;
    if (c != 1) { puts("FAIL: enum GREEN"); return 1; }

    c = BLUE;
    if (c != 2) { puts("FAIL: enum BLUE"); return 1; }

    /* 指定值的枚举 */
    enum month { JAN = 1, FEB = 2, MAR = 3, APR = 4 };
    if (JAN != 1) { puts("FAIL: enum JAN"); return 1; }
    if (APR != 4) { puts("FAIL: enum APR"); return 1; }

    /* enum 作为整数类型 */
    int x = RED;
    if (x != 0) { puts("FAIL: enum to int"); return 1; }

    /* sizeof enum */
    if (sizeof(enum color) != sizeof(int)) { puts("FAIL: sizeof enum"); return 1; }

    puts("PASS");
    return 0;
}