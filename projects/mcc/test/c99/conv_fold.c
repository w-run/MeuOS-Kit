/* Regression: MIR constant folding of float<->int conversions.
 *
 * chibicc cast/float tests exposed two MIR folding bugs:
 *  - MOP_F2I folded every float->int as 32-bit, so (long)2e15 became
 *    INT_MIN instead of 2000000000000000;
 *  - MOP_I2F treated the source as signed, so
 *    (double)(unsigned long)(long)-1 folded to -1.0 instead of
 *    18446744073709551616.0.  The unsigned path now uses MOP_UI2F.
 */
extern int puts(const char *);

int main(void) {
    /* F2I: 64-bit destination must keep the full range */
    if ((long)2e15 != 2000000000000000) { puts("FAIL: (long)2e15"); return 1; }
    /* 32-bit destination still truncates (implementation-defined) */
    if ((int)2e15 != -2147483648) { puts("FAIL: (int)2e15"); return 2; }

    /* I2F unsigned source */
    if ((double)(unsigned long)(long)-1 != 18446744073709551616.0) {
        puts("FAIL: (double)(unsigned long)(long)-1"); return 3;
    }
    if ((double)(unsigned int)4294967295u != 4294967295.0) {
        puts("FAIL: (double)uint max"); return 4;
    }

    /* runtime (non-constant) paths too */
    unsigned long u = 18446744073709551615ul;
    if ((double)u != 18446744073709551616.0) { puts("FAIL: runtime u"); return 5; }
    unsigned int v = 4294967295u;
    if ((double)v != 4294967295.0) { puts("FAIL: runtime v"); return 6; }

    return 0;
}
