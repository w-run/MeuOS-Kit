/* C++ operator chaining with temporaries, reference variables, and
 * value/reference overload resolution (C.2.3).
 *
 * - `a + b + c`: operator+ returns a temporary Vec; the chain reuses it.
 * - `x == a + b`: bool-returning operator== on a chained temporary.
 * - `Counter &r = base`: reference variable declaration (auto-deref).
 * - `f(Vec)` vs `f(Vec &)`: reference overloads mangle distinctly.
 */
extern int puts(const char *);

class Vec {
public:
    Vec(int v) { m = v; }
    Vec operator+(Vec o) { Vec r(m + o.m); return r; }
    bool operator==(Vec o) { return m == o.m; }
    int m;
};

class Counter {
public:
    Counter(int v) { val = v; }
    int val;
};

class X {
public:
    int f(Vec o) { return 1; }
    int f(Vec &o) { return 2; }
};

int main(void) {
    Vec a(1), b(2), c(3);
    Vec sum = a + b + c;
    if (sum.m != 6) { puts("FAIL: chained operator+"); return 1; }

    Vec x(3);
    bool eq = x == a + b;
    if (!eq) { puts("FAIL: bool operator== on temp"); return 2; }

    Counter base(42);
    Counter &r = base;
    if (r.val != 42) { puts("FAIL: reference variable"); return 3; }

    X obj;
    if (obj.f(a) != 2) { puts("FAIL: lvalue prefers ref overload"); return 4; }
    puts("PASS");
    return 0;
}
