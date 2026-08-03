/* aggregate_return.cc — C++ ≤16B class return via SysV registers + binding.
 *
 * C++ regression for the x86_64 MIR-native ≤16B aggregate-return bug:
 * classes returned by value through the register path (and consumed
 * through chained temporaries / structured bindings).
 *
 * Covers:
 *  - 12B class returned by value via its constructor (RAX + RDX)
 *  - 16B class with a double + int member (XMM0 + RDX)
 *  - a chained by-value temporary reuse (register live-range guard)
 *  - structured binding over a returned aggregate
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
extern int puts(const char *);

class Triple {
public:
    Triple(int x, int y, int z) { a = x; b = y; c = z; }
    int a, b, c;
};

Triple
mk(int base)
{
    return Triple(base, base + 1, base + 2);
}

class Mixed {
public:
    Mixed(double d, int i) { v = d; n = i; }
    double v;
    int n;
};

Mixed
mkmixed(double d, int n)
{
    return Mixed(d, n);
}

class Box {
public:
    Box(int v) { m = v; }
    Box operator+(Box o) { Box r(m + o.m); return r; }
    int m;
};

struct Pair { int x; int y; };

Pair
make_pair(void)
{
    Pair p;
    p.x = 3;
    p.y = 4;
    return p;
}

int
main(void)
{
    Triple t = mk(10);
    if (t.a != 10 || t.b != 11 || t.c != 12) { puts("FAIL: triple"); return 1; }

    Mixed m = mkmixed(2.5, 7);
    if (m.v != 2.5 || m.n != 7) { puts("FAIL: mixed"); return 2; }

    /* chained by-value returns through temporaries (register reuse) */
    Box a(1), b(2), c(3);
    Box sum = a + b + c;
    if (sum.m != 6) { puts("FAIL: chain"); return 3; }

    /* structured binding over a returned aggregate */
    auto [x, y] = make_pair();
    if (x != 3 || y != 4) { puts("FAIL: binding"); return 4; }

    puts("PASS");
    return 0;
}
