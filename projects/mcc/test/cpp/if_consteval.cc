/* if_consteval.cc — C++23 `if consteval` / `if !consteval` (P1938), m++.
 *
 * The branch is selected by whether the statement is in a constant-
 * evaluated context:
 *   - a constexpr/consteval function called with constant arguments is
 *     folded at compile time, so `if consteval` takes the consteval branch;
 *   - the same function called with a runtime argument (or an ordinary
 *     function) takes the ordinary/else branch at runtime.
 *
 * Covers:
 *   - constexpr function: consteval branch on constant args, else branch
 *     on runtime args
 *   - ordinary (non-constexpr) function: always the runtime branch
 *   - `if !consteval` (inverted)
 *   - `if consteval` with no else (runtime falls through)
 *   - interaction with `if constexpr` (sizeof-based)
 *   - template constexpr function
 *   - static_assert through the consteval branch
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

constexpr int csum(int n) {
    if consteval {
        return n * 2;
    } else {
        return n + 100;
    }
}

int ord(int n) {
    if consteval {
        return 1;
    } else {
        return 2;
    }
}

constexpr int cinv(int n) {
    if !consteval {
        return n + 100;
    } else {
        return n * 2;
    }
}

constexpr int cnoelse(int n) {
    if consteval {
        return n * 2;
    }
    return n + 100;
}

template <typename T>
constexpr int ctmpl(T x) {
    if consteval {
        return 1;
    } else {
        return 2;
    }
}

/* interaction with if constexpr: `if consteval` nested in the selected
 * branch of a (sizeof-based) `if constexpr` */
constexpr int cboth(int x) {
    if constexpr (sizeof(int) == 4) {
        if consteval {
            return 10;
        } else {
            return 20;
        }
    } else {
        return 30;
    }
}

int g_v;

int
main(void)
{
    /* constexpr function: constant args fold to the consteval branch */
    constexpr int a1 = csum(10);
    if (a1 != 20) return 1;
    /* runtime args take the ordinary branch */
    g_v = 5;
    if (csum(g_v) != 105) return 2;
    int v = g_v + 1;
    if (csum(v) != 106) return 3;

    /* ordinary function is never constant-evaluated */
    if (ord(0) != 2) return 4;

    /* `if !consteval` is inverted */
    constexpr int a5 = cinv(10);
    if (a5 != 20) return 5;
    if (cinv(g_v) != 105) return 6;

    /* no-else: runtime falls through */
    constexpr int a7 = cnoelse(10);
    if (a7 != 20) return 7;
    if (cnoelse(g_v) != 105) return 8;

    /* template constexpr function */
    constexpr int a9 = ctmpl(5);
    if (a9 != 1) return 9;
    if (ctmpl(g_v) != 2) return 10;

    /* mixed if constexpr + if consteval */
    constexpr int a11 = cboth(1);          /* sizeof(int)==4 */
    if (a11 != 10) return 11;
    if (cboth(g_v) != 20) return 12;

    /* static_assert through the consteval branch */
    static_assert(csum(7) == 14, "if consteval branch in static_assert");

    printf("if_consteval: all passed\n");
    return 0;
}
