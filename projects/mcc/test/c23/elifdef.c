/* Test C23 #elifdef / #elifndef */
int main(void) {
	int result = 0;

#define X
#ifdef X
	result += 1;
#elifdef Y  /* should be skipped */
	result += 10;
#else
	result += 100;
#endif
	/* result == 1 */

#ifndef Z
	result += 2;
#elifndef X  /* should be skipped (X is defined) */
	result += 20;
#endif
	/* result == 3 */

	return result == 3 ? 0 : 1;
}
