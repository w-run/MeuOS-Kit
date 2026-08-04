/* decltype.cc — C++11 `decltype` type deduction.
 *
 * `decltype(expr)` names the type of an expression without evaluating
 * it:
 *   - `decltype(a)`  (unparenthesized name): the declared type of the
 *     entity `a` (an `int` variable gives `int`);
 *   - `decltype(f(0))` / `decltype(g())` (function call): the return
 *     type;
 *   - `decltype((a))` (parenthesized expression): the type of the
 *     expression — a modifiable lvalue gives `T&`.
 *
 * Returns 0 on success.
 */

int
f(int x)
{
	return x + 1;
}

int
g(void)
{
	return 7;
}

int
main(void)
{
	int a = 3;
	decltype(a) b = 4;          /* b: int */
	decltype(f(0)) c = f(5);    /* c: int */
	decltype(g()) d = 9;        /* d: int */
	if (b != 4) return 1;
	if (c != 6) return 2;
	if (d != 9) return 3;

	/* parenthesized lvalue -> reference type: decltype((a)) is int&,
	 * so writes through `ref` mutate `a`. */
	decltype((a)) ref = a;
	ref = 42;
	if (a != 42) return 4;

	return 0;
}
