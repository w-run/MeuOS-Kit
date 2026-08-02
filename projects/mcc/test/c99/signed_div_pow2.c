/* Signed division/remainder by a power of two (§6.5.5).
 *
 * C semantics: division truncates toward zero, remainder takes the sign of
 * the dividend.  A fold that rewrites signed div/rem by 2^n into an
 * arithmetic shift / mask is only valid for unsigned values; MIR's
 * strength-reduction previously did exactly that and miscompiled negatives
 * (e.g. -7/2 -> -4 instead of -3, -7%4 -> 1 instead of -3).  Regression
 * guard for the fix (passes.c msimp_block, 2026-08-03).
 */
extern int puts(const char *);

int sdiv2(int x) { return x / 2; }
int sdiv4(int x) { return x / 4; }
int sdiv8(int x) { return x / 8; }
int srem2(int x) { return x % 2; }
int srem4(int x) { return x % 4; }
int srem8(int x) { return x % 8; }

int main(void) {
    /* Exact regression cases from defect V (worker-fold, 2026-08-03):
     * -7/2, -7%4, -17/8, -17%8.  Truncate toward zero, not toward -inf. */
    if (sdiv2(-7) != -3)        { puts("FAIL: -7/2"); return 1; }
    if (srem4(-7) != -3)        { puts("FAIL: -7%4"); return 1; }
    if (sdiv8(-17) != -2)       { puts("FAIL: -17/8"); return 1; }
    if (srem8(-17) != -1)       { puts("FAIL: -17%8"); return 1; }

    if (sdiv4(-9) != -2)        { puts("FAIL: -9/4"); return 1; }
    if (sdiv2(-8) != -4)        { puts("FAIL: -8/2"); return 1; }
    if (srem2(-7) != -1)        { puts("FAIL: -7%2"); return 1; }
    if (srem2(-8) != 0)         { puts("FAIL: -8%2"); return 1; }

    /* positive dividends stay correct */
    if (sdiv2(7) != 3)          { puts("FAIL: 7/2"); return 1; }
    if (sdiv4(9) != 2)          { puts("FAIL: 9/4"); return 1; }
    if (srem2(7) != 1)          { puts("FAIL: 7%2"); return 1; }
    if (srem8(17) != 1)         { puts("FAIL: 17%8"); return 1; }

    puts("PASS");
    return 0;
}
