/* C23 digit separators (6.4.4) edge cases beyond bin_literal.c:
 *   - separator inside a floating constant (integer and fraction parts)
 *   - separator inside a floating exponent
 *   - multiple consecutive separators `1'2'3'4`
 *   - separator immediately after the `0x` base prefix
 *   - separator inside a long hex constant
 */
int main(void) {
	/* floating constant with separators */
	double f = 1'000.5'5;                 /* 1000.55 */
	if (f < 1000.54 || f > 1000.56) return 1;

	/* integer with repeated separators */
	int x = 1'2'3'4;                      /* 1234 */
	if (x != 1234) return 2;

	/* separator right after base prefix */
	int z = 0x1'2'3;                      /* 0x123 */
	if (z != 0x123) return 3;

	/* long hex constant with separator */
	long y = 0xFFFF'FFFFL;                /* 4294967295 */
	if (y != 0xFFFFFFFFL) return 4;

	/* separator inside exponent */
	double e = 1.5e1'0;                   /* 1.5e10 = 15000000000 */
	if (e < 1.49e10 || e > 1.51e10) return 5;

	return 0;
}
