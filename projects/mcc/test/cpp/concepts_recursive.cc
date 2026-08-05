/* concepts_recursive.cc — concept bodies referencing other concepts
 * recursively (m++).
 *
 * A concept body may name other concepts; the reference chain is expanded
 * recursively at instantiation time (guarded by MAX_CONSTRAINT_DEPTH = 256).
 *
 * Covers:
 *  - a long reference chain (C0..C16, a 16-level nesting):
 *    every level is a single-concept body referencing the previous one, so
 *    the whole chain folds down to `sizeof(T) == 4`
 *  - `&&`/`||` composition across two levels of recursion (D1 uses D0 by
 *    name, D2 combines D1 with an || branch)
 *  - two satisfying types (int and float are both 4 bytes) exercising the
 *    deep chain; a non-satisfying type is rejected by the requires-clause
 *    (see concepts_recursive.neg.cc for the over-limit case)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> concept C0 = sizeof(T) == 4;
template <typename T> concept C1 = C0<T>;
template <typename T> concept C2 = C1<T>;
template <typename T> concept C3 = C2<T>;
template <typename T> concept C4 = C3<T>;
template <typename T> concept C5 = C4<T>;
template <typename T> concept C6 = C5<T>;
template <typename T> concept C7 = C6<T>;
template <typename T> concept C8 = C7<T>;
template <typename T> concept C9 = C8<T>;
template <typename T> concept C10 = C9<T>;
template <typename T> concept C11 = C10<T>;
template <typename T> concept C12 = C11<T>;
template <typename T> concept C13 = C12<T>;
template <typename T> concept C14 = C13<T>;
template <typename T> concept C15 = C14<T>;
template <typename T> concept C16 = C15<T>;

template <typename T> requires C16<T> T next(T x) { return x + 1; }

template <typename T> concept D0 = sizeof(T) <= 4;
template <typename T> concept D1 = D0<T> && sizeof(T) != 1;
template <typename T> concept D2 = D1<T> || sizeof(T) == 8;

template <typename T> requires D2<T> int pick(T x) { return 1; }

int
main(void)
{
    /* int satisfies the whole 16-level chain */
    if (next(41) != 42) return 1;
    /* float is also 4 bytes: the deep chain folds for a second type */
    if (next(1.5f) != 2.5f) return 2;

    /* D2 combines a two-level recursion with an || branch: int satisfies
     * D1 (sizeof<=4 and !=1), long (8 bytes) takes the || branch */
    if (pick(3) != 1) return 3;
    if (pick(3L) != 1) return 4;

    return 0;
}
