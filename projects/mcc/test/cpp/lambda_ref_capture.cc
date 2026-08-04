/* lambda_ref_capture.cc — C++14 lambda reference captures, default
 * captures, and init-captures (m++ end-to-end).
 *
 * Covers:
 *  - `[&x]`: by-reference capture — the closure sees the live variable
 *  - `[&]`: default by-reference capture of all enclosing locals
 *  - `[=, &y]`: default by-value + explicit by-reference override
 *  - `[&, y]`: default by-reference + explicit by-value override
 *  - `[x, &y]`: mixed by-value / by-reference captures
 *  - `[n = expr]`: init-capture introducing a new closure member
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int
main(void)
{
    /* reference capture: the closure reads through to the live variable */
    int x = 10;
    auto f = [&x](int v) { return x + v; };
    if (f(5) != 15) return 1;
    x = 100;
    if (f(5) != 105) return 2;   /* sees the updated x */

    /* init-capture: a new closure member n initialized from 42 */
    auto g = [n = 42](int v) { return n + v; };
    if (g(1) != 43) return 3;

    /* default reference capture `[&]` picks up every enclosing local */
    {
        int a = 2, b = 3;
        auto h = [&]() { return a * b; };
        if (h() != 6) return 4;
        a = 7;
        if (h() != 21) return 5;     /* by reference: sees the update */
    }

    /* default by-value + by-reference override `[=, &y]` */
    {
        int a = 2, y = 5;
        auto k = [=, &y]() { return a + y; };   /* a captured by value */
        if (k() != 7) return 6;
        a = 100;                    /* the by-value snapshot is frozen */
        y = 50;                     /* the by-reference override is live */
        if (k() != 52) return 7;
    }

    /* default by-reference + by-value override `[&, z]` */
    {
        int a = 100, z = 1;
        auto m = [&, z]() { return a + z; };    /* a captured by reference */
        if (m() != 101) return 8;
        a = 3;                      /* by-reference: live */
        z = 1000;                   /* by-value snapshot: frozen */
        if (m() != 4) return 9;
    }

    /* mixed explicit captures `[x, &y]` */
    {
        int x = 50, y = 3;
        auto n = [x, &y]() { return x + y; };   /* x by value, y by reference */
        if (n() != 53) return 10;
        x = 1;
        y = 2;
        if (n() != 52) return 11;   /* x snapshot (50) + y live (2) */
    }

    return 0;
}
