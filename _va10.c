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
    if (sum_args(5, 10, 20, 30, 40, 50) != 150) {
        printf("FAIL: sum_args\n");
        return 1;
    }
    if (vsum(3, 100, 200, 300) != 600) {
        printf("FAIL: vsum\n");
        return 2;
    }
    printf("ALL PASS\n");
    return 0;
}