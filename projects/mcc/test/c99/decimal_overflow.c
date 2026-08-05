/* Regression: an unsuffixed decimal integer constant that exceeds
 * LLONG_MAX has no type in its signed-only list (C11 6.4.4.1; the value
 * is undefined).  chibicc wraps it into signed long long (two's
 * complement), so 18446744073709551615 >> 63 == -1.  Match that.
 */
extern int puts(const char *);

int main(void) {
    /* folded in MIR: decimal constant wraps to signed long long */
    if ((long long)(18446744073709551615 >> 63) != -1) {
        puts("FAIL: >>63"); return 1;
    }
    if (sizeof(18446744073709551615) != 8) { puts("FAIL: sizeof"); return 2; }

    /* still usable as unsigned long long when assigned */
    unsigned long long x = 18446744073709551615;
    if (x != 18446744073709551615ULL) { puts("FAIL: as ull"); return 3; }

    /* hex constant keeps its unsigned type */
    if ((18446744073709551615ULL >> 63) != 1) { puts("FAIL: hex"); return 4; }
    return 0;
}
