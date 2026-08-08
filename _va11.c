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

static void print_mixed(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p == 'd') printf("%d ", va_arg(ap, int));
        else if (*p == 's') printf("%s ", va_arg(ap, char *));
        else if (*p == 'f') printf("%.1f ", va_arg(ap, double));
        else if (*p == 'l') printf("%lld ", va_arg(ap, long long));
    }
    va_end(ap);
    printf("\n");
}

int main(void)
{
    /* Test 1: sum_args */
    if (sum_args(5, 10, 20, 30, 40, 50) != 150) {
        printf("FAIL: sum_args\n");
        return 1;
    }

    /* Test 2: vsum cross-function */
    if (vsum(3, 100, 200, 300) != 600) {
        printf("FAIL: vsum cross-function\n");
        return 2;
    }

    /* Test 3: mixed-type varargs */
    print_mixed("dsfl", 1, "two", 3.0, 4LL);

    /* Test 4: long long constant */
    long long big = 9999999999LL;
    if (big != 9999999999LL) {
        printf("FAIL: long long=%lld\n", big);
        return 3;
    }

    /* Test 5: snprintf */
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%d %s %f %lld %x %c",
        1, "two", 3.0, 4LL, 0xff, 'A');
    if (n < 0) {
        printf("FAIL: snprintf\n");
        return 4;
    }

    printf("ALL PASS\n");
    return 0;
}