/* constexpr.cc — C++11 constexpr functions + static_assert (m++ phase 1).
 *
 * Covers:
 *  - constexpr function definitions (single `return expr;` bodies)
 *  - compile-time folding of constexpr calls with integer constant args
 *  - static_assert pass cases at file scope
 *  - constexpr variables initialized from folded constexpr calls, and
 *    their use in later constant expressions
 *  - recursion (factorial), ternary, shift/or, negation
 *  - runtime calls with constant args (folded) and non-constant args
 *    (must emit a real call)
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
constexpr int add(int a, int b) { return a + b; }
constexpr int mul(int a, int b) { return a * b; }
constexpr int sq(int x) { return x * x; }
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr int pick(int c, int a, int b) { return c ? a : b; }
constexpr int neg(int x) { return -x; }
constexpr unsigned int shl(unsigned int v) { return v << 2; }

/* file-scope static_asserts are evaluated at compile time */
static_assert(add(2, 3) == 5, "add folds");
static_assert(mul(3, 4) == 12, "mul folds");
static_assert(sq(5) == 25, "sq folds");
static_assert(add(mul(2, 3), sq(2)) == 10, "nested constexpr call");
static_assert(fact(5) == 120, "recursive constexpr");
static_assert(fact(0) == 1, "factorial base case");
static_assert(pick(1, 7, 9) == 7, "ternary true arm");
static_assert(pick(0, 7, 9) == 9, "ternary false arm");
static_assert(neg(6) == -6, "unary minus");
static_assert(shl(3) == 12, "shift");
static_assert((shl(3) | 1) == 13, "shift then or");

/* constexpr variables: folded initializer, usable in later constant
 * expressions (chains through the constant value captured at define). */
constexpr int v1 = add(10, 20);
constexpr int v2 = sq(v1);
constexpr int v3 = fact(v2 > 100 ? 5 : 3);
static_assert(v1 == 30, "constexpr var 1");
static_assert(v2 == 900, "constexpr var chain");
static_assert(v3 == 120, "constexpr var feeds call");

int
main(void)
{
    /* runtime call with constant args folds to the constant */
    if (add(1, 2) != 3) return 1;
    if (sq(3) != 9) return 2;

    /* runtime call with non-constant args must emit a real call */
    int x = 4;
    if (sq(x) != 16) return 3;
    int y = 3;
    if (mul(x, y) != 12) return 4;

    /* constexpr variables usable at runtime too */
    if (v1 != 30) return 5;
    if (v2 != 900) return 6;

    /* recursion at runtime */
    if (fact(6) != 720) return 7;

    return 0;
}
