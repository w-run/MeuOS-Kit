/* concepts_combo.cc — C++20 concept composition with `&&` in the concept
 * body (m++).
 *
 * A concept body may be a conjunction of constant conditions:
 *   concept SmallAndInt = sizeof(T) <= 4 && sizeof(T) == 4;
 * which is the m++ analogue of `Small && NotVoid`.  m++ folds the
 * conjunction at instantiation time.
 *
 * Covers:
 *  - a conjunction body combining two type-property conditions
 *  - satisfying and non-satisfying instantiations (the non-satisfying
 *    one is only exercised via the requires-clause being false)
 *  - the composed concept reused across multiple deduced types
 *
 * NOTE: two forms of `&&` composition are NOT supported yet and are
 * covered by .neg.cc tests:
 *   - `requires Small<T> && NotVoid<T>` in a requires-clause
 *     (concepts_req_and.neg.cc)
 *   - `concept C = Small<T> && NotVoid<T>` referencing concept names in
 *     the body (only bare constant expressions are foldable)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
/* "Small"  -> at most 4 bytes; "NotVoid" -> exactly 4 bytes.  Combined
 * with && directly in the concept body (the foldable form). */
template <typename T> concept Small = sizeof(T) <= 4;
template <typename T> concept NotVoid = sizeof(T) == 4;

template <typename T> concept SmallAndInt = sizeof(T) <= 4 && sizeof(T) == 4;

template <typename T> requires SmallAndInt<T> T next(T x) { return x + 1; }

int
main(void)
{
    /* int satisfies both conditions (4 bytes, non-void) */
    if (next(41) != 42) return 1;

    /* a second 4-byte type satisfies the conjunction too */
    if (next(7) != 8) return 2;

    /* the composed concept constrains a third instantiation */
    if (next((int)9) != 10) return 3;

    return 0;
}
