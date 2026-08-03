/* C23 standard attributes boundary (6.7.11 / 6.7.12):
 *   - [[nodiscard]] on a function
 *   - [[deprecated("message")]] carrying a string argument
 *   - [[unsequenced]] (C23 new attribute)
 *   - [[reproducible]] (C23 new attribute)
 *
 * Extends attributes.c by exercising attribute *arguments* and the two
 * new C23 function-effect attributes.
 */
[[nodiscard]] int make_val(void) { return 1; }

[[deprecated("use make_val() instead")]]
int legacy_val(void) { return 2; }

[[unsequenced]] int bump(int x) { return x + 1; }

[[reproducible]] int twice(int x) { return x * 2; }

int main(void) {
	int a = make_val();
	int b = legacy_val();        /* deprecated but still callable */
	int c = bump(3);
	int d = twice(4);
	(void)make_val();            /* discard is allowed (just warned) */
	if (a != 1) return 1;
	if (b != 2) return 2;
	if (c != 4) return 3;
	if (d != 8) return 4;
	return 0;
}
