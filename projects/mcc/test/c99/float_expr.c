/* float_expr.c — C99 complex floating-point expressions.
 *
 * Uses exactly-representable values (powers of two / halves) so `==`
 * comparisons are exact.  Covers:
 *  - mixed int/float arithmetic chains with implicit promotion
 *  - float/double interaction in expressions and parameters
 *  - floating arrays summed in a loop (accumulator carries state)
 *  - floating values returned from and passed to functions
 *  - casts between float widths and to/from int
 *  - comparisons and logical use of float expressions
 */
extern int puts(const char *);

static double sum3(double a, double b, double c) { return a + b + c; }
static float half(float x) { return x * 0.5f; }

int main(void) {
    /* mixed int/float promotion in a chain */
    if (1 + 2.5 != 3.5)             { puts("FAIL: 1+2.5"); return 1; }
    if (2 * 1.5 + 1 != 4.0)         { puts("FAIL: 2*1.5+1"); return 2; }
    if (10 / 4.0 != 2.5)            { puts("FAIL: 10/4.0"); return 3; }
    if (10.0 / 4 != 2.5)            { puts("FAIL: 10.0/4"); return 4; }

    /* float <-> double interaction */
    if (0.5f + 0.25 != 0.75)        { puts("FAIL: float+double"); return 5; }
    if (half(4.0f) != 2.0f)         { puts("FAIL: half"); return 6; }

    /* exact binary-fraction arithmetic */
    if (0.5 + 0.25 + 0.125 != 0.875) { puts("FAIL: 0.875"); return 7; }
    if (1.5 * 2.0 - 0.5 != 2.5)     { puts("FAIL: chain"); return 8; }
    if (3.0 / 2.0 != 1.5)           { puts("FAIL: 3.0/2.0"); return 9; }

    /* function params/returns with floats */
    if (sum3(1.5, 2.5, 3.0) != 7.0) { puts("FAIL: sum3"); return 10; }
    if (sum3(0.5, 0.25, 0.25) != 1.0) { puts("FAIL: sum3 halves"); return 11; }

    /* array accumulation */
    {
        double a[4] = { 0.5, 1.5, 2.0, 3.0 };
        double s = 0.0;
        int i;
        for (i = 0; i < 4; i = i + 1)
            s = s + a[i];
        if (s != 7.0)               { puts("FAIL: array sum"); return 12; }
    }

    /* casts */
    if ((double)(float)2.5 != 2.5)  { puts("FAIL: dbl->flt->dbl"); return 13; }
    if ((float)(int)3.75 != 3.0f)   { puts("FAIL: (int)3.75"); return 14; }
    if ((int)-3.75 != -3)           { puts("FAIL: (int)-3.75"); return 15; }
    if ((double)(int)0.5 != 0.0)    { puts("FAIL: (int)0.5"); return 16; }

    /* comparisons and logical use */
    if (!(2.5 > 2.25))              { puts("FAIL: 2.5>2.25"); return 17; }
    if (2.5 >= 2.75)                { puts("FAIL: 2.5>=2.75"); return 18; }
    if (!(1.0 == 1.0))              { puts("FAIL: 1.0==1.0"); return 19; }
    if (!(0.0 ? 1 : 0) == 0)        { puts("FAIL: 0.0 truthiness"); return 20; }

    /* nested function calls mixing types */
    if (sum3(half(2.0f), 1.5, 0.5) != 3.0) { puts("FAIL: nested"); return 21; }

    puts("PASS");
    return 0;
}
