/* concepts_req_and.cc — C++20 `requires A<T> && B<T>` composition in a
 * requires-clause (m++).
 *
 * The constraint is a boolean expression over concept uses; `&&`, `||`
 * and `!` are evaluated recursively at instantiation time.
 *
 * Covers:
 *  - `requires Small<T> && NotVoid<T>` (conjunction of two concepts)
 *  - a satisfying instantiation; a non-satisfying type is rejected
 *    (exercised only via a negative return, see concepts_req_neg.cc)
 *  - `||` and `!` combos
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> concept Small = sizeof(T) <= 4;
template <typename T> concept NotVoid = sizeof(T) == 4;
template <typename T> concept Signed = (T)-1 < (T)0;

template <typename T> requires Small<T> && NotVoid<T> T next(T x) { return x + 1; }
template <typename T> requires Small<T> || Signed<T> int orc(T x) { return 1; }
template <typename T> requires !Small<T> int big(T x) { return 1; }

int
main(void)
{
    /* int satisfies Small && NotVoid */
    if (next(41) != 42) return 1;
    if (next(7) != 8) return 2;

    /* int satisfies Small || Signed (both true) */
    if (orc(3) != 1) return 3;

    /* long (8 bytes) does not satisfy Small, so `!Small` holds */
    if (big(3L) != 1) return 4;

    return 0;
}
