/* constexpr_return_obj.cc — C++20 constexpr functions returning class
 * objects, evaluated in constant expressions.
 *
 * `constexpr P make_p(int x) { P p = {x, x*2}; return p; }` — the
 * aggregate local + class return are interpreted by the constexpr
 * statement interpreter, and member accesses on the call result
 * (`make_p(3).a`) fold through the mini memory model.
 *
 * Returns 0 on success. */
struct P { int a; int b; };

constexpr P make_p(int x) { P p = {x, x*2}; return p; }

static_assert(make_p(3).a == 3, "member a");
static_assert(make_p(3).b == 6, "member b");
static_assert(make_p(10).a + make_p(10).b == 30, "two calls");

int main(void) {
    constexpr P q = make_p(7);       /* local constexpr class var from call */
    if (q.a != 7 || q.b != 14) return 1;
    return 0;
}
