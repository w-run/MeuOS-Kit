/* NEGATIVE C23 test: an invalid digit separator (two adjacent apostrophes)
 * is not permitted — a separator must sit between two digits (6.4.4).
 *
 * Expected: mcc error. Verified by run-neg.sh expecting non-zero compile exit.
 */
int main(void) {
	int x = 1''0;       /* invalid: adjacent separators */
	return x;
}
