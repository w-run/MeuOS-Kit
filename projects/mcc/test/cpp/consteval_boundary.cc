/* consteval_boundary.cc — C++20 consteval edge cases (m++).
 *
 * Covers:
 *  - deep nesting of consteval calls (sq(sq(sq(2))))
 *  - consteval calls passed as arguments to another consteval call
 *  - consteval recursion with constant arguments (factorial)
 *  - consteval results used in larger constant arithmetic
 *  - a call with a non-constant argument is now a compile-time error
 *    (immediate invocation must be a constant expression); see
 *    consteval_nonconst.neg.cc
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
consteval int
sq(int x)
{
    return x * x;
}

consteval int
add(int a, int b)
{
    return a + b;
}

consteval int
fact(int n)
{
    return n <= 1 ? 1 : n * fact(n - 1);
}

int
main(void)
{
    /* deep nesting: sq(sq(sq(2))) = sq(sq(4)) = sq(16) = 256 */
    if (sq(sq(sq(2))) != 256) return 1;

    /* consteval calls as arguments to another consteval call */
    if (add(sq(3), sq(4)) != 25) return 2;
    if (add(fact(3), fact(4)) != 30) return 3;

    /* consteval recursion folds at compile time */
    if (fact(5) != 120) return 4;
    if (fact(8) != 40320) return 5;

    /* results used in larger constant arithmetic */
    if (sq(4) + sq(3) != 25) return 6;
    if (fact(4) * 2 != 48) return 7;

    return 0;
}
