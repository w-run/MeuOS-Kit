#include <stdio.h>
#include <stdarg.h>

static int sum_args(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}

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
    print_mixed("dsfl", 1, "two", 3.0, 4LL);

    long long big = 9999999999LL;
    if (big != 9999999999LL) {
        printf("FAIL: long long=%lld\n", big);
        return 3;
    }

    printf("ALL PASS\n");
    return 0;
}
