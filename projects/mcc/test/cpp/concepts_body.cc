/* concepts_body.cc — a concept body referencing other named concepts
 * (recursive constraint expansion, m++).
 *
 *   concept SmallAndInt = Small<T> && NotVoid<T>;
 *
 * uses `Small` and `NotVoid` by name; when the composed concept is used
 * in a requires-clause the body is expanded recursively into plain
 * constant expressions and folded at instantiation time.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> concept Small = sizeof(T) <= 4;
template <typename T> concept NotVoid = sizeof(T) == 4;
template <typename T> concept SmallAndInt = Small<T> && NotVoid<T>;

template <typename T> requires SmallAndInt<T> T next(T x) { return x + 1; }

int
main(void)
{
    /* int satisfies both sub-concepts */
    if (next(41) != 42) return 1;
    if (next((int)9) != 10) return 2;

    return 0;
}
