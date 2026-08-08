#include <stdarg.h>

static int sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int s = 0;
    for (int i = 0; i < count; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    return sum(3, 10, 20, 30) == 60 ? 0 : 1;
}
