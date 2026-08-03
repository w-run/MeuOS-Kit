/* lambda_capture_constexpr.cc — by-value lambda capture combined with
 * constexpr folding (m++).
 *
 * lambda.cc / lambda_capture_class.cc cover the capture mechanics; this
 * file pins the *snapshot* boundary: a by-value capture must copy the
 * variable at closure-creation time, so mutating the original afterwards
 * must not change what the closure returns.  It also mixes a captured
 * `constexpr` variable into the body so the folded constant and the
 * captured copy coexist in the same closure.
 *
 * Covers:
 *  - `[v]` value capture, read back after the source variable is mutated
 *  - a captured block-scope `constexpr` variable folded from a constexpr
 *    function call
 *  - a capturing lambda that also takes parameters (with and without an
 *    explicit trailing return type)
 *  - two closures capturing the same variable independently
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
constexpr int sq(int x) { return x * x; }

int
main(void)
{
    constexpr int n = sq(4);   /* folded to 16 */
    int v = 7;

    auto f = [v]() { return v * 2; };
    auto g = [n, v]() { return n + v; };
    auto p = [v](int k) { return v * k; };
    auto q = [n](int k) -> int { return n - k; };

    if (n != 16) return 1;
    if (f() != 14) return 2;
    if (g() != 23) return 3;
    if (p(3) != 21) return 4;
    if (q(6) != 10) return 5;

    /* by-value capture is a snapshot: mutating v must not be observed */
    v = 100;
    if (f() != 14) return 6;
    if (g() != 23) return 7;
    if (p(2) != 14) return 8;

    /* a closure created after the mutation sees the new value */
    auto f2 = [v]() { return v * 2; };
    if (f2() != 200) return 9;
    if (f() != 14) return 10;

    return 0;
}
