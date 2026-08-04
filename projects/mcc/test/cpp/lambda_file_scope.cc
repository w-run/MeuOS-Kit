/* lambda_file_scope.cc — file-scope and constexpr lambdas (m++).
 *
 * A no-capture lambda may be declared at file scope: its closure class
 * is synthesized as usual and the closure object gets static storage.
 * Because the closure is empty, it is constant-constructible — no
 * runtime construction is needed, so a plain `auto f = [](...){...};`
 * works as a static initializer and `constexpr auto f = [](...){...};`
 * satisfies the constant-initializer requirement.  The closure's
 * operator() is const by default, so the const closure object is
 * callable too.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
auto add = [](int a, int b) { return a + b; };          /* file scope lambda */

constexpr auto twice = [](int x) { return x * 2; };     /* constexpr lambda */

static auto negate = [](int x) -> int { return -x; };   /* file scope, explicit ret */

int
main(void)
{
    if (add(2, 3) != 5) return 1;
    if (add(-1, 1) != 0) return 2;

    if (twice(21) != 42) return 3;
    if (twice(0) != 0) return 4;

    if (negate(7) != -7) return 5;

    return 0;
}
