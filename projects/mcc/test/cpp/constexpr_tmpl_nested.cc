/* constexpr_tmpl_nested.cc — constexpr *function templates* nested inside
 * one another (m++).
 *
 * constexpr_tmpl_sizeof.cc covers `sizeof` inside a template constexpr;
 * this file exercises the instantiate-then-fold boundary: a constexpr
 * function template whose body calls another constexpr template, so the
 * inner instantiation must be produced *and* folded before the outer one
 * can be evaluated.
 *
 * Covers:
 *  - twice<T> called twice from quad<T> (nested instantiation + folding)
 *  - the same templates instantiated for int and double
 *  - a file-scope `constexpr` initializer folded through both levels
 *  - a template constexpr folding a `sizeof` argument
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> constexpr T twice(T x) { return x + x; }
template <typename T> constexpr T quad(T x) { return twice(twice(x)); }

/* folded at file scope: quad<int>(3) = 12 */
constexpr int c1 = quad(3);

int
main(void)
{
    /* file-scope fold through two template levels */
    if (c1 != 12) return 1;

    /* the double instantiation of the same chain */
    if (quad(2.5) != 10.0) return 2;
    if (twice(1.25) != 2.5) return 3;

    /* block-scope constexpr folding a sizeof argument */
    constexpr int n = twice(sizeof(int));
    if (n != 8) return 4;

    /* runtime contexts reuse the cached instantiations */
    if (quad(0) != 0) return 5;
    if (twice(-7) != -14) return 6;

    return 0;
}
