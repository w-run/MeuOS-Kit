/* if_constexpr.cc — C++17 `if constexpr` (compile-time branching, m++).
 *
 * The condition must be a compile-time constant; the unselected branch is
 * discarded entirely (skipped at the token level) so it is never
 * instantiated — the whole point of if constexpr inside templates.
 *
 * Covers:
 *  - `if constexpr (1)` / `if constexpr (0)` with else
 *  - the unselected branch calling an undefined function (must not be
 *    emitted)
 *  - `if constexpr` inside a function template, choosing different
 *    branches per instantiation (`sizeof(T) == 4`)
 *  - `else if constexpr` chains
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

/* if constexpr inside a function template: each instantiation picks the
 * matching branch by compile-time evaluation of sizeof(T). */
template <typename T>
T
pick(T x)
{
    if constexpr (sizeof(T) == 4) {
        return x + 1;   /* int (4 bytes) */
    } else if constexpr (sizeof(T) == 8) {
        return x + 2;   /* double (8 bytes) */
    } else {
        return x + 3;   /* char (1 byte) */
    }
}

int
main(void)
{
    /* literal constant conditions select/discard branches */
    if constexpr (1) {
    } else {
        return 1; /* must be discarded */
    }
    if constexpr (0) {
        return 2; /* must be discarded */
    } else {
    }

    /* a discarded branch calls an undefined function: it is never
     * emitted, so compilation and linking must succeed */
    if constexpr (1) {
    } else {
        int r = undefined_fn_not_emitted();
        (void)r;
    }

    /* template instantiations pick different branches */
    if (pick(5) != 6) return 10;           /* int → +1 */
    if (pick((char)1) != 4) return 11;     /* char → +3 */
    if (pick(1.0) != 3.0) return 12;       /* double → +2 */

    return 0;
}
