/* C90 unsigned 类型修饰符（§6.7.2） */

extern int puts(const char *);

int main(void)
{
    /* unsigned char */
    unsigned char uc = 255;
    if (uc != 255) { puts("FAIL: unsigned char max"); return 1; }

    /* unsigned short */
    unsigned short us = 65535;
    if (us != 65535) { puts("FAIL: unsigned short max"); return 1; }

    /* unsigned int */
    unsigned int ui = 4294967295u;
    if (ui != 4294967295u) { puts("FAIL: unsigned int max"); return 1; }

    /* unsigned long */
    unsigned long ul = 4294967295UL;
    if (ul != 4294967295UL) { puts("FAIL: unsigned long max"); return 1; }

    /* 无符号回绕 */
    unsigned char uc2 = 255;
    uc2 = uc2 + 1;
    if (uc2 != 0) { puts("FAIL: unsigned wrap"); return 1; }

    /* sizeof unsigned */
    if (sizeof(unsigned char) != 1)           { puts("FAIL: sizeof unsigned char"); return 1; }
    if (sizeof(unsigned int) != sizeof(int))  { puts("FAIL: sizeof unsigned int"); return 1; }

    puts("PASS");
    return 0;
}