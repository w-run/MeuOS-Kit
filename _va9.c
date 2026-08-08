#include <stdio.h>
#include <stdarg.h>

int main(void)
{
    /* Test snprintf with various format specifiers */
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%d %s %f %lld %x %c",
        1, "two", 3.0, 4LL, 0xff, 'A');
    if (n < 0) {
        printf("FAIL: snprintf\n");
        return 4;
    }
    printf("snprintf OK: %s\n", buf);

    printf("ALL PASS\n");
    return 0;
}