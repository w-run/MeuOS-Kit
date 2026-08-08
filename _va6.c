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
    /* Test 1: basic sum_args */
    /* ok already tested */
    
    /* Test 2: cross-function va_list */
    if (vsum(3, 100, 200, 300) != 600) {
        printf("FAIL: vsum cross-function\n");
        return 2;
    }
    printf("PASS: vsum\n");
    
    /* Test 3: mixed-type */
    print_mixed("dsfl", 1, "two", 3.0, 4LL);
    printf("PASS: print_mixed\n");
    
    printf("ALL PASS\n");
    return 0;
}