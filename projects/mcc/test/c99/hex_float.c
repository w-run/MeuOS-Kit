/* C99 hexadecimal floating constants (6.4.4.2).
 *
 * Exercises the lexer's number() scanner (p/P binary exponent with sign)
 * and expr_primary.c's TNUMBER float-vs-int discrimination
 * (strpbrk base==16 ? ".pP"), which hands the literal to strtod.
 * Regression guard for mcc bootstrap: self-hosted mcc (linked against
 * meuos-libc) failed to compile any source containing a hex float.
 */
extern int puts(const char *);

int main(void) {
    /* 0x1p63 == 2^63 */
    if (0x1p63 != 9223372036854775808.0)          { puts("FAIL: 0x1p63"); return 1; }
    /* 0x1.8p3 == (1 + 8/16) * 2^3 == 12.0 */
    if (0x1.8p3 != 12.0)                          { puts("FAIL: 0x1.8p3"); return 2; }
    /* float suffix on a hex float */
    if (0x1.8p3f != 12.0f)                        { puts("FAIL: 0x1.8p3f"); return 3; }
    /* negative binary exponent */
    if (0x1p-2 != 0.25)                           { puts("FAIL: 0x1p-2"); return 4; }
    /* unary minus applied to a hex float literal */
    if (-0x1p4 != -16.0)                          { puts("FAIL: -0x1p4"); return 5; }
    /* uppercase 0X / P, signed exponent */
    if (0X1P+3 != 8.0)                            { puts("FAIL: 0X1P+3"); return 6; }
    /* lowercase 0x, uppercase P, fractional mantissa */
    if (0x1.8P-2 != 0.375)                        { puts("FAIL: 0x1.8P-2"); return 7; }
    /* hex digit 'f' in mantissa, multi-digit exponent */
    if (0x1.fp10 != 1984.0)                       { puts("FAIL: 0x1.fp10"); return 8; }
    /* 15-digit mantissa: ~pi, exercises full-precision accumulation */
    if (0x1.921fb54442d18p1 != 3.141592653589793) { puts("FAIL: 0x1.921fb54442d18p1"); return 9; }
    /* plain hex integer constant (must NOT take the float path) */
    if (0x10 != 16)                               { puts("FAIL: 0x10 hex int"); return 10; }

    puts("PASS hex float");
    return 0;
}
