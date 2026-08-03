/* fold_expr.cc (SKIP — pending/ regression marker)
 *
 * C++17 fold expressions are not yet supported by m++: the unary fold
 * `(... + args)` is rejected with "expected primary expression".
 * Kept here as a regression marker for when fold expressions land.
 */
template <typename... A>
auto
sum(A... a)
{
    return (... + a);
}

template <typename... A>
auto
all_true(A... a)
{
    return (... && a);
}

int
main(void)
{
    if (sum(1, 2, 3) != 6) return 1;
    if (!all_true(true, true, true)) return 2;
    return 0;
}
