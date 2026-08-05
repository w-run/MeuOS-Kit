/* constexpr_struct_return.cc — C++11 constexpr functions returning an
 * aggregate (struct) whose members are assigned member-by-member.
 *
 * Unlike the aggregate-initializer form (`P p = {x, x*2}`), a struct
 * local initialized without an initializer and then filled with member
 * assignments (`P p; p.x = ...; p.y = ...;`) is supported too: each
 * constant member assignment is recorded into a mini memory model, and
 * the folded aggregate can be used in constant-expression contexts.
 *
 * Returns 0 on success.
 */

struct P {
	int x;
	int y;
};

constexpr P
mk(int a, int b)
{
	P p;
	p.x = a;
	p.y = b;
	return p;
}

/* file-scope static_asserts drive constant folding of the call results */
static_assert(mk(3, 4).x == 3, "member x of constexpr aggregate folds");
static_assert(mk(3, 4).y == 4, "member y of constexpr aggregate folds");

int
main(void)
{
	constexpr P p = mk(10, 20);
	if (p.x != 10) return 1;
	if (p.y != 20) return 2;

	/* distinct folded results, reused in later constant expressions */
	constexpr P r = mk(30, 40);
	if (r.x + r.y != 70) return 3;

	return 0;
}
