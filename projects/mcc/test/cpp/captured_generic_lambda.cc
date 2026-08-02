/* captured_generic_lambda.cc — captured generic lambda (defect G, m++).
 *
 * `[x, y](auto t) { return x * t + y; }` — a generic (auto-parameter)
 * lambda with by-value captures.  Each call-site argument type
 * instantiates its own operator() template instance; captured members
 * must resolve to `(*this).member` with the closure's this offset.
 *
 * Defect G was a segfault/wrong value: the instantiated operator() body
 * resolved captures against the caller's locals (its scope chain reached
 * the call site) instead of the closure-class members, so a second
 * capture was read as `this` itself.  Fixed by instantiating the
 * operator() in the owner class's declaration scope so captures go
 * through cpp_member_ident (`(*this).offset`).
 *
 * Covers single/multiple captures, double captures, struct captures,
 * multiple instantiations (int and double argument types), and
 * coexistence with a plain (non-captured) generic lambda.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Pair { int a; int b; };

int
main(void)
{
    /* single capture */
    int base = 100;
    auto f = [base](auto x) { return base + x; };
    if (f(5) != 105) return 1;

    /* multiple captures: second capture must use this+4, not this */
    int x = 3, y = 4;
    auto g = [x, y](auto t) { return x * t + y; };
    if (g(2) != 10) return 2;
    if (g(10) != 34) return 3;

    /* double capture */
    double d = 1.5;
    auto h = [d](auto t) { return d + t; };
    if (h(2) != 3.5) return 4;

    /* struct capture */
    Pair p = {10, 20};
    auto k = [p](auto t) { return p.a + p.b + t; };
    if (k(1) != 31) return 5;

    /* two instantiations (int and double argument types) */
    auto m = [x, y](auto t) { return x * t + y; };
    if (m(2) != 10) return 6;
    if (m(2.0) != 10.0) return 7;

    /* plain generic lambda still works alongside */
    auto n = [](auto t) { return t * 2; };
    if (n(5) != 10) return 8;

    return 0;
}
