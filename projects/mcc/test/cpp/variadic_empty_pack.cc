/* variadic_empty_pack.cc — empty parameter-pack expansion boundary (m++).
 *
 * variadic.cc covers `sizeof...` on an empty pack; this file pins the
 * harder case: *expanding* an empty pack at a call site, so `f(args...)`
 * must degenerate into a zero-argument call rather than passing a stray
 * argument.  It also relays an empty pack through two variadic levels.
 *
 * Covers:
 *  - `zero(args...)` with an empty pack -> a genuine zero-argument call
 *  - a one-element pack expanded into a one-parameter template
 *  - an empty pack relayed through two nested variadic templates
 *  - a non-empty pack through the same relay chain (mixed types)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

/* zero-argument, non-template target of an empty pack expansion */
int zero() { return 42; }

/* one-parameter target of a single-element pack expansion */
template <typename T> int one(T a) { return a; }

template <typename... Args> int fwd0(Args... args) { return zero(args...); }
template <typename... Args> int fwd1(Args... args) { return one(args...); }

/* two-level relay: relay1 -> relay0 -> cnt */
template <typename... Args> int cnt(Args... args) { return sizeof...(Args); }
template <typename... Args> int relay0(Args... args) { return cnt(args...); }
template <typename... Args> int relay1(Args... args) { return relay0(args...); }

int
main(void)
{
    /* empty pack expanded into a zero-argument call */
    if (fwd0() != 42) return 1;

    /* single-element pack expanded into a one-parameter template */
    if (fwd1(9) != 9) return 2;
    if (fwd1(2.5) != 2) return 3;   /* one<double> truncated on return */

    /* empty pack relayed through two variadic levels */
    if (relay1() != 0) return 4;
    if (relay0() != 0) return 5;

    /* non-empty mixed pack through the same relay chain */
    if (relay1(1, 2.5, 'c') != 3) return 6;
    if (relay0(1) != 1) return 7;

    return 0;
}
