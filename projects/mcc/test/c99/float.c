/* C99 float/double type conversions (chibicc-derived subset) */
extern int puts(const char *);

/* Helper: compare integer-truncated float values */
static int ieq(double a, double b) { return (int)a == (int)b; }

int main(void) {
    /* Type promotion to float */
    if (!ieq(35, (float)(char)35))           { puts("FAIL: (float)(char)"); return 1; }
    if (!ieq(35, (float)(int)35))            { puts("FAIL: (float)(int)"); return 1; }
    if (!ieq(35, (float)(long)35))           { puts("FAIL: (float)(long)"); return 1; }
    if (!ieq(35, (float)(unsigned int)35))   { puts("FAIL: (float)(unsigned)"); return 1; }

    /* Type promotion to double */
    if (!ieq(35, (double)(char)35))          { puts("FAIL: (double)(char)"); return 1; }
    if (!ieq(35, (double)(int)35))           { puts("FAIL: (double)(int)"); return 1; }
    if (!ieq(35, (double)(long)35))          { puts("FAIL: (double)(long)"); return 1; }

    /* Float/double to integer */
    if (!ieq(35, (int)(float)35))            { puts("FAIL: (int)(float)"); return 1; }
    if (!ieq(35, (int)(double)35))           { puts("FAIL: (int)(double)"); return 1; }
    if (!ieq(35, (long)(double)35))          { puts("FAIL: (long)(double)"); return 1; }

    /* Float comparison */
    if (!(2.0 == 2))                         { puts("FAIL: 2.0==2"); return 1; }
    if (5.1 < 5)                             { puts("FAIL: 5.1<5"); return 1; }
    if (!(4.9 < 5))                          { puts("FAIL: 4.9<5"); return 1; }
    if (5.1 <= 5)                            { puts("FAIL: 5.1<=5"); return 1; }
    if (!(4.9 <= 5))                         { puts("FAIL: 4.9<=5"); return 1; }

    /* Float arithmetic — compare truncated values */
    if (!ieq(6, 2.3 + 3.8))                  { puts("FAIL: 2.3+3.8"); return 1; }
    if (!ieq(-1, 2.3 - 3.8))                 { puts("FAIL: 2.3-3.8"); return 1; }
    if (!ieq(-3, -3.8))                      { puts("FAIL: -3.8"); return 1; }
    if (!ieq(13, 3.3 * 4))                   { puts("FAIL: 3.3*4"); return 1; }
    if (!ieq(2, 5.0 / 2))                    { puts("FAIL: 5.0/2"); return 1; }

    /* Logical: float as truth value */
    if (!ieq(0, !3.))                        { puts("FAIL: !3."); return 1; }
    if (!ieq(1, !0.))                        { puts("FAIL: !0."); return 1; }

    /* Ternary with float */
    if (!ieq(5, 0.0 ? 3 : 5))               { puts("FAIL: 0.0?3:5"); return 1; }
    if (!ieq(3, 1.2 ? 3 : 5))               { puts("FAIL: 1.2?3:5"); return 1; }

    puts("PASS");
    return 0;
}
