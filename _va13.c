#include <stdio.h>
#include <stdarg.h>

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
    /* Just test 3: mixed-type varargs */
    print_mixed("dsfl", 1, "two", 3.0, 4LL);
    printf("PASS: print_mixed\n");

    /* Test 4: long long constant */
    long long big = 9999999999LL;
    printf("big assigned\n");
    if (big != 9999999999LL) {
        printf("FAIL: long long=%lld\n", big);
        return 3;
    }
    printf("PASS: long long\n");

    printf("ALL PASS\n");
    return 0;
}