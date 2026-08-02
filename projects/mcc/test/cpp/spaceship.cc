/* spaceship.cc — C++20 three-way comparison operator `<=>` (m++).
 *
 * Implemented as a simplified int result: (a <=> b) is -1 when a < b,
 * 0 when a == b, +1 when a > b.  The lexer recognizes `<=>` (previously
 * it would tokenize as `<=` `>`).
 *
 * Covers int/double/char operands, variables, the result used in
 * comparisons/assignments, and coexistence with `<=`, `<`, `<<`, `>`.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int
main(void)
{
    /* literal comparisons */
    if ((3 <=> 5) != -1) return 1;
    if ((5 <=> 5) != 0) return 2;
    if ((7 <=> 5) != 1) return 3;

    /* variables */
    int a = 10, b = 20;
    if ((a <=> b) != -1) return 4;
    if ((b <=> a) != 1) return 5;
    if ((a <=> a) != 0) return 6;

    /* floating point */
    double x = 1.5, y = 2.5;
    if ((x <=> y) != -1) return 7;
    if ((y <=> x) != 1) return 8;

    /* char */
    char c1 = 'a', c2 = 'b';
    if ((c1 <=> c2) != -1) return 9;

    /* result in comparisons and assignments */
    if ((a <=> b) < 0) { } else { return 10; }
    int r = (a <=> b);
    if (r != -1) return 11;

    /* coexistence with existing relational/shift operators */
    if (!(a <= b)) return 12;
    if ((a << 1) != 20) return 13;
    if ((a < b) != 1) return 14;
    if ((b >= a) != 1) return 15;

    return 0;
}
