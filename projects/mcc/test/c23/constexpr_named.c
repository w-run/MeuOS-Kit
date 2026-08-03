/* C23 constexpr objects used in constant-expression contexts.
 *
 * Regression for F3: a *named* constexpr variable must be folded into
 * an integer constant expression so it is usable in _Static_assert,
 * array dimensions and case labels — not just at runtime.
 */
constexpr int K = 9;
constexpr int M = K * 2;            /* 18: constexpr reads constexpr */
_Static_assert(K == 9, "K must fold to 9");
_Static_assert(M == 18, "M must fold to 18");

/* array dimension requiring an integer constant expression */
int arr[K + 1];                     /* size 10 */
_Static_assert(sizeof(arr) == 10 * sizeof(int), "arr size must be 10*int");

/* constexpr function result assigned to a named constexpr object */
constexpr int square(int x) { return x * x; }
constexpr int S = square(6);        /* 36 */
_Static_assert(S == 36, "S must fold to 36");

/* case label uses the folded constexpr value */
static int classify(int x)
{
	switch (x) {
	case K:  return 1;              /* 9 */
	case M:  return 2;              /* 18 */
	default: return 0;
	}
}

int main(void) {
	if (K != 9) return 1;
	if (M != 18) return 2;
	if (S != 36) return 3;
	if (classify(9) != 1) return 4;
	if (classify(18) != 2) return 5;
	if (classify(0) != 0) return 6;
	return 0;
}
