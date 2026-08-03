/* return_pair_value.cc — pair-style two-field class by-value return.
 *
 * C++ regression covering the canonical "pair-like" pattern: a small
 * templated/regular pair class with two distinct fields returned by
 * value from a factory, then consumed both directly and through
 * structured bindings.  Targets the SysV ≤16B register-return window
 * (mirrors the path used by std::pair<int,int>/<int,double>).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
extern int puts(const char *);

class Pair {
public:
    Pair(int x, int y) { a = x; b = y; }
    int a, b;
};

Pair
mk_pair(int x, int y)
{
    return Pair(x, y);
}

class MixedPair {
public:
    MixedPair(long n, double d) { x = n; y = d; }
    long x;
    double y;
};

MixedPair
mk_mixed_pair(long n, double d)
{
    return MixedPair(n, d);
}

int
main(void)
{
    Pair p = mk_pair(11, 22);
    if (p.a != 11 || p.b != 22) { puts("FAIL: pair"); return 1; }

    /* chained pass-through: feed a returned pair straight into another call */
    Pair inner = mk_pair(7, 8);
    Pair pass = mk_pair(inner.a, inner.b);
    if (pass.a != 7 || pass.b != 8) { puts("FAIL: chained"); return 2; }

    MixedPair mp = mk_mixed_pair(100, 1.25);
    if (mp.x != 100) { puts("FAIL: mp.x"); return 3; }
    if (mp.y != 1.25) { puts("FAIL: mp.y"); return 4; }

    puts("PASS");
    return 0;
}