/* C23 constexpr functions: nested calls and recursion.
 *
 * Covers:
 *  - nested constexpr calls (a constexpr function calling another
 *    constexpr function several levels deep, folding at compile time)
 *  - direct recursion (factorial) folded at compile time
 *  - mutual recursion (even/odd) folded at compile time
 *  - the same recursive functions also work at runtime with a
 *    non-constant argument (a runtime definition is emitted too)
 */
constexpr int add(int a, int b) { return a + b; }
constexpr int mul(int a, int b) { return a * b; }
constexpr int deep(int x) { return add(mul(x, x), add(x, 1)); }  /* x*x + x + 1 */

constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

constexpr int is_odd(int n);   /* mutual recursion: forward declaration */
constexpr int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
constexpr int is_odd(int n)  { return n == 0 ? 0 : is_even(n - 1); }

/* nested folds: deep(deep(2)) = deep(7) = 49 + 7 + 1 = 57 */
constexpr int d1 = deep(3);         /* 9 + 3 + 1 = 13 */
constexpr int d2 = deep(deep(2));   /* 57 */
constexpr int f1 = fact(5);         /* 120 */
constexpr int f2 = fact(8);         /* 40320 */
constexpr int e1 = is_even(10);     /* 1 */
constexpr int o1 = is_odd(9);       /* 1 */

int main(void) {
	if (d1 != 13) return 1;
	if (d2 != 57) return 2;
	if (f1 != 120) return 3;
	if (f2 != 40320) return 4;
	if (e1 != 1) return 5;
	if (o1 != 1) return 6;
	if (is_even(7) != 0) return 7;

	/* runtime path: non-constant arguments still call the emitted
	 * definition (mutation keeps the compiler from folding it) */
	int x = 4;
	if (fact(x) != 24) return 8;
	if (deep(x) != 21) return 9;
	return 0;
}
