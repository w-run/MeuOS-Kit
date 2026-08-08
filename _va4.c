#include <stdio.h>
#include <stdarg.h>

int main(void)
{
    long long big = 9999999999LL;
    if (big != 9999999999LL) {
        printf("FAIL: long long=%lld\n", big);
        return 1;
    }
    printf("OK: long long constant\n");
    return 0;
}