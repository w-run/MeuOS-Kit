/* constexpr_body.cc — C++23 constexpr statement interpreter (multi-
 * statement bodies, P2242 groundwork / C++14 relaxed constexpr), m++.
 *
 * Covers constant evaluation of constexpr function bodies that are more
 * than `{ return <expr>; }`:
 *   - local integer variables + compound assignment (`s += 2`)
 *   - if/else branches
 *   - for / while / do-while loops (with the loop variable as a local or
 *     as a function parameter being mutated)
 *   - break / continue inside loops
 *   - multiple return statements
 *   - nested constexpr calls (a multi-statement function calling another)
 *   - reads of global `constexpr` variables
 *   - the runtime path regression: the same body still emits a correct
 *     runtime definition when the call cannot be constant-folded
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

/* local variable + compound assignment */
constexpr int add2(int n) { int s = n; s += 2; return s; }

/* if/else with multiple returns */
constexpr int pick(int n) { if (n > 3) return n + 1; return n - 1; }

/* for loop */
constexpr int sum(int n) { int s = 0; for (int i = 1; i <= n; i++) s += i; return s; }

/* while loop mutating the parameter */
constexpr int cnt(int n) { int c = 0; while (n > 0) { n--; c++; } return c; }

/* do-while */
constexpr int dwsum(int n) { int s = 0, i = 1; do { s += i; i++; } while (i <= n); return s; }

/* break inside a loop */
constexpr int brk(int n) { int s = 0; for (int i = 0; i < n; i++) { if (i == 2) break; s += i; } return s; }

/* continue inside a loop */
constexpr int ctn(int n) { int s = 0; for (int i = 0; i < n; i++) { if (i == 1) continue; s += i; } return s; }

/* nested constexpr calls */
constexpr int sq(int x) { return x * x; }
constexpr int sqf(int n) { int s = sq(n); s += sq(2); return s; }

/* factorial with *= */
constexpr int fact(int n) { int r = 1; for (int i = 2; i <= n; i++) r *= i; return r; }

/* power with while + parameter mutation + compound assignment */
constexpr int pw(int b, int e) { int r = 1; while (e > 0) { r *= b; e--; } return r; }

/* global constexpr variable read inside the body */
constexpr int K = 10;
constexpr int kadd(int n) { return n + K; }

/* n-ary condition chain: `else if`-style via plain if/else-if */
constexpr int tier(int n) {
    if (n < 0) return 0;
    if (n < 10) return 1;
    if (n < 100) return 2;
    return 3;
}

/* runtime regression: same multi-statement body called with a runtime
 * argument must still produce a correct runtime definition. */
constexpr int runadd2(int n) { int s = n; s += 2; return s; }
int g_v;

int
main(void)
{
    constexpr int a1 = add2(5);
    if (a1 != 7) return 1;
    constexpr int a2 = pick(5);
    constexpr int a3 = pick(2);
    if (a2 != 6 || a3 != 1) return 2;
    constexpr int a4 = sum(4);
    if (a4 != 10) return 3;
    constexpr int a5 = cnt(5);
    if (a5 != 5) return 4;
    constexpr int a6 = dwsum(4);
    if (a6 != 10) return 5;
    constexpr int a7 = brk(5);
    if (a7 != 1) return 6;            /* 0 + 1, break at i == 2 */
    constexpr int a8 = ctn(4);
    if (a8 != 5) return 7;            /* 0 + 2 + 3, skip i == 1 */
    constexpr int a9 = sqf(3);
    if (a9 != 13) return 8;           /* 3*3 + 2*2 */
    constexpr int a10 = fact(5);
    if (a10 != 120) return 9;
    constexpr int a11 = pw(2, 10);
    if (a11 != 1024) return 10;
    constexpr int a12 = kadd(5);
    if (a12 != 15) return 11;
    constexpr int a13 = tier(5);
    constexpr int a14 = tier(50);
    constexpr int a15 = tier(500);
    constexpr int a16 = tier(-3);
    if (a13 != 1 || a14 != 2 || a15 != 3 || a16 != 0) return 12;
    static_assert(add2(5) == 7, "constexpr body in static_assert");

    /* runtime call of a multi-statement constexpr function */
    g_v = 5;
    if (runadd2(g_v) != 7) return 13;
    /* runtime call where the argument is a runtime value */
    int n = g_v + 1;
    if (runadd2(n) != 8) return 14;

    printf("constexpr_body: all passed\n");
    return 0;
}
