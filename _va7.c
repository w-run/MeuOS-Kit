#include <stdio.h>
#include <stdarg.h>

static int vsum_helper(va_list ap, int count)
{
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += va_arg(ap, int);
    return sum;
}

static int vsum(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    int result = vsum_helper(ap, count);
    va_end(ap);
    return result;
}

int main(void)
{
    /* Test cross-function va_list forwarding */
    if (vsum(3, 100, 200, 300) != 600) {
        printf("FAIL: vsum cross-function\n");
        return 2;
    }
    printf("PASS: vsum\n");

    /* Test long long constant */
    long long big = 9999999999LL;
    if (big != 9999999999LL) {
        printf("FAIL: long long=%lld\n", big);
        return 3;
    }
    printf("PASS: long long constant\n");

    printf("ALL PASS\n");
    return 0;
}