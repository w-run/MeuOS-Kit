/* variadic_explicit.cc — explicit template arguments to a variadic function.
 *
 * Regression for cpp_tmpl_deduce: explicit template arguments were dropped
 * after the first element when the parameter list ended in a pack
 * (`count<int, double>` collected only `int`, so sizeof...(Ts) was 1 and
 * the mangle was `count_i`).  The deduction loop must keep filling the pack
 * with every trailing explicit argument, and an empty `<>` must yield an
 * empty pack.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

template <typename... Ts> int count() { return sizeof...(Ts); }

/* a fixed parameter, then a pack, all via explicit template args */
template <typename T, typename... Rest>
int icount() { return sizeof...(Rest) + 1; }

int
main(void)
{
    /* explicit type-argument lists must all reach the pack */
    if (count<>() != 0) return 1;                 /* empty explicit list */
    if (count<int>() != 1) return 2;
    if (count<int, double>() != 2) return 3;
    if (count<int, double, char>() != 3) return 4;

    /* fixed + explicit pack */
    if (icount<int>() != 1) return 5;
    if (icount<int, char>() != 2) return 6;
    if (icount<int, char, short>() != 3) return 7;

    return 0;
}
