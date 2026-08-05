/* consteval.cc — C++20 consteval (immediate functions, m++).
 *
 * A consteval function is evaluated at compile time: every call with
 * constant arguments folds to its result via the constexpr evaluator
 * (the callee is marked isconstexpr and eval() folds the call).  Unlike
 * a strict C++20 consteval, a call with non-constant arguments here
 * degrades to a normal runtime call rather than an error.
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

int
main(void)
{
    /* constant arguments fold at compile time */
    if (sq(5) != 25) return 1;
    if (sq(sq(2)) != 16) return 2;       /* nested consteval calls */
    if (add(3, 4) != 7) return 3;

    /* consteval calls inside a consteval call */
    if (add(sq(3), sq(4)) != 25) return 4;

    /* compile-time result usable as an array bound */
    int arr[sq(3)];
    arr[0] = 42;
    if (arr[0] != 42) return 5;

    return 0;
}
