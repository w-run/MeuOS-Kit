/* Test C23 labeled break/continue */
int main(void) {
	int i, result = 0;

	/* Test: labeled break from outer loop */
	outer:
	for (i = 0; i < 5; i++) {
		inner:
		for (int j = 0; j < 5; j++) {
			if (j == 1)
				break outer;  /* should exit outer loop */
			result++;
		}
	}

	/* After break outer, i == 0, result == 1 */
	if (result != 1) return 1;

	return 0;
}
