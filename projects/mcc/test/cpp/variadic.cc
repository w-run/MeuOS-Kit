/* variadic.cc — C++11 variadic templates (parameter packs, m++ end-to-end).
 *
 * Covers `template <typename... Args>` parsing, `sizeof...(Args)` (the
 * pack element count), and simple pack expansion `f(args...)` for
 * forwarding calls: a variadic forwarding to a fixed template, mixed
 * fixed + pack parameters, mixed-type packs, a variadic template calling
 * another variadic template, and empty packs.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
/* fixed two-argument template used as the forwarding target */
template <typename T> int add2(T a, T b) { return a + b; }

/* a variadic template forwarding its pack to a fixed template:
 * `call(3, 4)` expands `args...` to `add2(args_0, args_1)` */
template <typename... Args> int call(Args... args) { return add2(args...); }

/* sizeof...(Args): the pack element count */
template <typename... Args> int count(Args... args) {
    return sizeof...(Args);
}

/* a fixed parameter followed by a trailing pack */
template <typename T, typename... Rest>
int first(T head, Rest... tail) {
    return sizeof...(Rest) + 1;
}

/* mixed-type pack forwarded to a mixed-argument template */
template <typename T, typename U>
double mix(T a, U b) { return (double)a + b; }

template <typename... Args>
double call2(Args... args) { return mix(args...); }

/* a variadic template calling another variadic template */
template <typename... Args>
int relay(Args... args) { return first(args...); }

int
main(void)
{
    /* pack forwarding */
    if (call(3, 4) != 7) return 1;           /* Args = [int, int] */
    if (call(10, 20) != 30) return 2;        /* cache reuse */

    /* sizeof...(Args) */
    if (count(1, 2, 3) != 3) return 3;
    if (count(1.5) != 1) return 4;
    if (count() != 0) return 5;              /* empty pack */

    /* fixed parameter + pack */
    if (first(1, 2, 3, 4) != 4) return 6;    /* 3 Rest + 1 */
    if (first(5) != 1) return 7;             /* empty Rest */

    /* mixed-type pack forwarding */
    if (call2(1, 2.5) != 3.5) return 8;      /* int + double */
    if (call2(2, 3) != 5.0) return 9;        /* int + int */

    /* variadic calling variadic */
    if (relay(1, 2, 3) != 3) return 10;      /* relay -> first */
    if (relay(7) != 1) return 11;

    return 0;
}
