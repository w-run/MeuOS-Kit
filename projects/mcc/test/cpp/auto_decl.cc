/* auto_decl.cc — C++11 `auto` type deduction (m++ end-to-end).
 *
 * Covers `auto x = expr;` variable declarations (local + global, deduced
 * from arithmetic/pointer/expression initializers), `auto` interacting
 * with function templates (an auto variable taking a template result, and
 * an auto return type deduced from a template call), and C++14-style
 * `auto` function return types (free functions, member functions, and
 * chained auto calls).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
template <typename T> T max(T a, T b) { return a > b ? a : b; }

/* global `auto` variable: deduced from the initializer */
auto g_answer = 42;

/* C++14-style `auto` return types */
auto add(int a, int b) { return a + b; }
auto fdouble(double x) { return x * 2.0; }

/* multiple return statements with a consistent deduced type */
auto pick(int n) {
    if (n > 0) return max(3, 7);   /* deduced from a template call */
    return 0;
}

/* an auto function calling an auto function */
auto chain(int n) { return pick(n) + 1; }

class Calc {
public:
    int base;
    Calc() { base = 10; }
    auto get() { return base + 1; }         /* member auto return */
    auto add2() { return get() + 2; }       /* auto calling member auto */
    auto twice(double v) { return v * 2.0; }
};

int
main(void)
{
    /* basic auto variables */
    auto x = 5;
    if (x != 5) return 1;
    auto y = 3.5;
    if (y != 3.5) return 2;
    auto z = x + 2;                         /* from an expression */
    if (z != 7) return 3;
    auto p = &x;                            /* pointer type */
    if (*p != 5) return 4;

    /* global auto variable */
    if (g_answer != 42) return 5;

    /* auto + templates: an auto variable takes a template result */
    auto m = max(3, 7);
    if (m != 7) return 6;
    auto d = max(1.5, 2.5);
    if (d != 2.5) return 7;

    /* auto function return types */
    if (add(3, 4) != 7) return 8;
    if (fdouble(2.5) != 5.0) return 9;
    if (pick(1) != 7) return 10;
    if (pick(-1) != 0) return 11;
    if (chain(1) != 8) return 12;

    /* auto variable deduced from an auto function */
    auto r = add(1, 2);
    if (r != 3) return 13;

    /* member functions with auto return types */
    Calc c;
    if (c.get() != 11) return 14;
    if (c.add2() != 13) return 15;
    if (c.twice(1.5) != 3.0) return 16;

    return 0;
}
