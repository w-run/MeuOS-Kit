/* nttp_constexpr_member_call.cc — folding a constexpr member-function
 * call on a constexpr object.
 *
 * `constexpr int r = s.f(2)` must replay f's body with the object's
 * members bound (as the member variables of `this`) plus the explicit
 * argument, then fold the return expression.
 *
 * Four pieces were needed (all in the constexpr evaluator / member-decl
 * paths):
 *   - member functions are now marked isconstexpr (cpp_method.c), and
 *     their bodies are registered with the constant evaluator when the
 *     two-phase in-class body is flushed (flush_pending_methods).
 *   - cpp_constexpr_eval detects the call's hidden `&obj` `this` first
 *     argument, binds the object's members by name into the replay scope
 *     (via cpp_cexpr_member_value), and skips the (member-typed) `this`
 *     parameter when matching the explicit argument list.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

struct Point {
	int x;
	int y;
	constexpr int sum(int k) const { return x + y + k; }
};

struct S {
	int a;
	constexpr int f(int x) const { return a + x; }
};

constexpr Point p = { 10, 30 };
constexpr S    s = { 40 };

constexpr int	r1 = p.sum(2);   /* 10 + 30 + 2 = 42 */
constexpr int	r2 = s.f(2);     /* 40 + 2      = 42 */
constexpr int	r3 = p.sum(0);   /* 10 + 30 + 0 = 40 */
constexpr int	r4 = s.f(0);     /* 40          = 40 */

int
main(void)
{
	if (r1 != 42) return 1;
	if (r2 != 42) return 2;
	if (r3 != 40) return 3;
	if (r4 != 40) return 4;

	/* the folded constants agree with a runtime reformulation */
	struct Point rp = { 10, 30 };
	struct S    rs = { 40 };
	if (rp.sum(2) != r1) return 5;
	if (rs.f(2)  != r2) return 6;
	return 0;
}
