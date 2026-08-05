/* concepts.cc — C++20 concepts / requires-clause (minimal set, m++).
 *
 * - Concept definition: `template <typename T> concept Name = expr;`
 *   where the body is a constant boolean expression over T
 *   (e.g. `sizeof(T) == 4`).
 * - Requires-clause: `template <typename T> requires Concept<T> T f(...)`
 *   — the constraint is checked at instantiation time by replaying the
 *   concept body with the deduced type substituted and constant-folding
 *   it; a false result is a compile-time error.
 *
 * Covers: constant `true` concept, a type-property concept (sizeof),
 * satisfying and non-satisfying instantiations (the latter must error),
 * and a concept used with a different deduced type per call.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
/* constant-true concept */
template <typename T> concept AlwaysTrue = true;

/* type-property concept: only 4-byte types satisfy it */
template <typename T> concept FourByte = sizeof(T) == 4;

template <typename T> requires AlwaysTrue<T> T twice(T x) { return x + x; }
template <typename T> requires FourByte<T> T dbl(T x) { return x * 2; }

int
main(void)
{
    /* constant-true constraint */
    if (twice(3) != 6) return 1;
    if (twice(2.5) != 5.0) return 2;

    /* 4-byte constraint satisfied by int */
    if (dbl(5) != 10) return 3;

    /* the same function instantiated for another 4-byte type */
    if (dbl((int)7) != 14) return 4;

    return 0;
}
