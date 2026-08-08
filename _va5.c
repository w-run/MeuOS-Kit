#include <stdio.h>
#include <stdarg.h>

int main(void)
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%d %s %f %lld %x %c",
            1, "two", 3.0, 4LL, 0xff, 'A');
    if (n < 0) {
        printf("FAIL: snprintf\n");
        return 1;
    }
    printf("snprintf returned %d: %s\n", n, buf);
    printf("OK: snprintf\n");
    return 0;
}