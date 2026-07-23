/* C99 _Bool type (§6.2.5, §6.3.1.2) */
extern int puts(const char *);

int main(void) {
    _Bool b1 = 1;
    _Bool b2 = 0;
    _Bool b3 = 42;          /* non-zero becomes 1 */

    if (b1 != 1) { puts("FAIL: _Bool=1"); return 1; }
    if (b2 != 0) { puts("FAIL: _Bool=0"); return 1; }
    if (b3 != 1) { puts("FAIL: _Bool=42 -> 1"); return 1; }

    /* Cast to _Bool */
    if ((_Bool)0 != 0)   { puts("FAIL: (_Bool)0"); return 1; }
    if ((_Bool)1 != 1)   { puts("FAIL: (_Bool)1"); return 1; }
    if ((_Bool)100 != 1) { puts("FAIL: (_Bool)100"); return 1; }
    if ((_Bool)0.0 != 0) { puts("FAIL: (_Bool)0.0"); return 1; }
    if ((_Bool)0.5 != 1) { puts("FAIL: (_Bool)0.5"); return 1; }

    /* Size */
    if (sizeof(_Bool) != 1) { puts("FAIL: sizeof _Bool"); return 1; }

    puts("PASS");
    return 0;
}
