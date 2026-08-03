/* C23 constexpr functions used in compile-time-only contexts:
 *   - as operands of _Static_assert (constant expression required)
 *   - as an array dimension (VM-less constant size)
 *   - recursively (fact) and cross-function composed (compose -> add)
 *
 * This pushes beyond constexpr_func.c (which only checks storage values
 * and runtime definitions) into constexpr used where the value MUST be
 * folded at translation time.
 *
 * NOTE: a *named* constexpr variable initialised by constexpr calls is
 * only exercised at runtime here, not inside _Static_assert — see the
 * defect report (mcc does not yet fold a named constexpr var into an
 * integer constant expression for _Static_assert).
 */
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr int add(int a, int b) { return a + b; }
constexpr int compose(int x) { return add(add(x, x), x); }   /* 3 * x */

_Static_assert(fact(5) == 120, "fact(5) must be 120");
_Static_assert(compose(7) == 21, "compose(7) must be 21");

/* constexpr folded into an array size (constant expression needed) */
int arr[fact(4)];                                 /* size 24 */
_Static_assert(sizeof(arr) == 24 * sizeof(int), "arr size must be 24*int");

constexpr int K = fact(3) + compose(2);          /* 6 + 6 = 12 */

int main(void) {
	if (fact(5) != 120) return 1;
	if (compose(7) != 21) return 2;
	if (sizeof(arr) != 24 * sizeof(int)) return 3;
	int k = K;                                    /* named constexpr at runtime */
	if (k != 12) return 4;
	/* runtime definition still exists */
	int n = 4;
	if (fact(n) != 24) return 5;
	return 0;
}
