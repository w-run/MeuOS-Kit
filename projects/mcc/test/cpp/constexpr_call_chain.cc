/* constexpr_call_chain.cc — constexpr functions calling other constexpr
 * functions (nested evaluation chains, m++).
 *
 * constexpr.cc / constexpr_body.cc cover single-level folding; this file
 * exercises the *call chain* boundary: a constexpr function whose body
 * calls another constexpr function, five levels deep, plus self-recursion.
 *
 * Covers:
 *  - a 5-level chain (lvl0..lvl4) where each level calls every level below
 *    it, so lvl4 folds a 9-call evaluation tree
 *  - self-recursive constexpr folding (fact)
 *  - the folded value used in three positions: a file-scope `constexpr`
 *    initializer, an array bound, and a runtime expression
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

/* lvl0(x) = x + 1 */
constexpr int lvl0(int x) { return x + 1; }
/* lvl1(x) = 2*(x+1) */
constexpr int lvl1(int x) { return lvl0(x) * 2; }
/* lvl2(x) = lvl1 + lvl0 = 3x + 3 */
constexpr int lvl2(int x) { return lvl1(x) + lvl0(x); }
/* lvl3(x) = lvl2 - lvl1 = x + 1 */
constexpr int lvl3(int x) { return lvl2(x) - lvl1(x); }
/* lvl4(x) = lvl3 + lvl2 + lvl1 = 6x + 6 */
constexpr int lvl4(int x) { return lvl3(x) + lvl2(x) + lvl1(x); }

/* self-recursive constexpr */
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

/* file-scope constexpr initializers folded through the whole chain */
constexpr int k4 = lvl4(3);   /* 6*3 + 6 = 24 */
constexpr int kf = fact(5);   /* 120 */

int
main(void)
{
    /* file-scope folding */
    if (k4 != 24) return 1;
    if (kf != 120) return 2;

    /* array bound: lvl2(1) = 3*1 + 3 = 6 */
    int a[lvl2(1)];
    if (sizeof(a) / sizeof(a[0]) != 6) return 3;

    /* the same chain evaluated in a runtime context */
    if (lvl4(3) != 24) return 4;
    if (lvl4(0) != 6) return 5;
    if (lvl3(9) != 10) return 6;

    /* recursion depth boundary: base case and a deeper call */
    if (fact(1) != 1) return 7;
    if (fact(0) != 1) return 8;
    if (fact(7) != 5040) return 9;

    return 0;
}
