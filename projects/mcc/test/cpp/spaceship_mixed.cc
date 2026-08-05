/* spaceship_mixed.cc — C++20 `<=>` mixed with relational operators.
 *
 * Covers:
 *  - `<=>` results used in `<`, `<=`, `>`, `>=` and `==` comparisons
 *  - no-space tokenization (`a<=>b` lexes as `<=>`, not `<=` `>`)
 *  - `<=>` results used numerically in arithmetic
 *  - C-style chained relationals (`a < b < c`) still evaluate
 *    left-to-right (the simplified m++ `<=>` is an int, so chaining
 *    keeps plain-C semantics)
 *  - comparing one `<=>` result against another `<=>` result
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int
main(void)
{
    int a = 10, b = 20, c = 15;

    /* <=> result fed into relational operators */
    if (!((a <=> b) < 0)) return 1;
    if (!((a <=> b) <= -1)) return 2;
    if (!((b <=> a) > 0)) return 3;
    if (!((b <=> a) >= 1)) return 4;
    if ((b <=> a) == 0) return 5;

    /* no-space tokenization: a<=>b must not parse as `a <= > b` */
    if ((a<=>b) != -1) return 6;

    /* <=> result used numerically */
    if ((a <=> b) + (b <=> a) != 0) return 7;  /* -1 + 1 */
    if ((b <=> c) * 2 != 2) return 8;          /* 1 * 2 */
    if ((c <=> a) * 3 != 3) return 9;          /* 1 * 3 */

    /* C-style chained relationals stay left-to-right */
    if ((a <= b) < (a <= c) != 0) return 10;   /* 1 < 1 = 0 */
    if (!(a < b < c)) return 11;               /* 1 < 15 = 1 */
    if (!(c < b < a)) return 12;               /* 1 < 10 = 1, counterintuitive */

    /* one <=> result compared against another */
    if (!((a <=> b) < (b <=> a))) return 13;   /* -1 < 1 */
    if ((a <=> a) != (b <=> b)) return 14;     /* 0 == 0 */

    return 0;
}
