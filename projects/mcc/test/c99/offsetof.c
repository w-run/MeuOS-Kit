/* C99 offsetof macro (§7.17) */
#include <stddef.h>
extern int puts(const char *);

typedef struct {
    int a;
    char b;
    int c;
    double d;
} T;

int main(void) {
    if (offsetof(T, a) != 0)               { puts("FAIL: offsetof a"); return 1; }
    if (offsetof(T, b) != 4)               { puts("FAIL: offsetof b"); return 1; }
    if (offsetof(T, c) != 8)               { puts("FAIL: offsetof c"); return 1; }
    if (offsetof(T, d) != 16)              { puts("FAIL: offsetof d"); return 1; }

    puts("PASS");
    return 0;
}
