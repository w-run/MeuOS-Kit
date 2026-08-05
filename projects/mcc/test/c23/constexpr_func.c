/* Test C23 constexpr functions — compile-time evaluation.
 *
 * A constexpr function called with constant integer arguments is folded
 * at compile time (like the C++ constexpr evaluator); the same function
 * also emits a normal runtime definition, so non-constant calls still
 * work.
 */
constexpr int square(int x) { return x * x; }
constexpr int add3(int a, int b, int c) { return a + b + c; }
constexpr int cube_sq(int x) { return square(x) * x; }
constexpr int s1 = square(6);        /* 36 */
constexpr int s2 = square(3) + add3(1, 2, 3);  /* 9 + 6 = 15 */
constexpr int s3 = cube_sq(4);       /* 16 * 4 = 64 */

int main(void) {
	if (s1 != 36) return 1;
	if (s2 != 15) return 2;
	if (s3 != 64) return 3;
	/* the runtime definition still exists: non-constant call works */
	int x = 7;
	if (square(x) != 49) return 4;
	if (add3(1, 2, x) != 10) return 5;
	return 0;
}
