/* lambda.cc — C++11 lambda expressions (anonymous-class lowering, m++
 * end-to-end).
 *
 * Covers no-capture lambdas (explicit `-> ret` and auto-deduced return),
 * by-value captures (single and multiple), the by-value snapshot
 * semantics, lambdas with and without parameters, nested lambdas, and a
 * lambda calling another lambda.  Reference captures are not supported
 * yet (rejected with a clear error).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
int
main(void)
{
    /* no-capture lambda, explicit return type */
    auto dbl = [](int x) -> int { return x * 2; };
    if (dbl(3) != 6) return 1;
    if (dbl(10) != 20) return 2;

    /* no-capture lambda, auto-deduced return type */
    auto add = [](int a, int b) { return a + b; };
    if (add(3, 4) != 7) return 3;

    /* no-capture, no params, no return type annotation */
    auto fortytwo = [] { return 42; };
    if (fortytwo() != 42) return 4;

    /* by-value capture (single) */
    int base = 5;
    auto scaled = [base](int x) { return base + x; };
    if (scaled(3) != 8) return 5;

    /* by-value capture (multiple) */
    int a = 10, b = 20;
    auto sum = [a, b]() -> int { return a + b; };
    if (sum() != 30) return 6;

    /* by-value snapshot: later modification of the captured variable does
     * not affect the captured copy */
    int v = 1;
    auto snap = [v]() -> int { return v + 1; };
    v = 100;
    if (snap() != 2) return 7;

    /* two lambdas side by side */
    auto mul = [](int a, int b) -> int { return a * b; };
    if (mul(3, 4) != 12) return 8;

    /* lambda calling a lambda */
    auto outer = [](int a) -> int {
        auto inner = [](int b) -> int { return b * 2; };
        return inner(a) + 1;
    };
    if (outer(5) != 11) return 9;

    /* nested lambda with a capture */
    auto nested = [](int x) -> int {
        int scale = 3;                       /* local of the outer body */
        auto inner = [scale](int y) -> int { return scale + y; };
        return inner(x);
    };
    if (nested(4) != 7) return 10;

    return 0;
}
