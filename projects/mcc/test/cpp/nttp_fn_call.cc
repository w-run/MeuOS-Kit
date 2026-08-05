/* nttp_fn_call.cc - constexpr function calls as non-type template args.
 *
 * Regression for NTTP arguments that are constexpr function calls:
 *   arrsize<sq(3)>()
 * Previously the call `sq(3)` inside the explicit template-argument replay
 * hit expr_postfix's template-call lowering and consumed the *outer*
 * template's pending placeholder, so deduction saw the wrong template /
 * argument count and rejected the construct ("too many arguments for
 * function call").  A call only goes through template instantiation when
 * the callee is actually the placeholder (decl == dummy callee); a normal
 * constexpr function call must not pop the outer template's stack.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

constexpr int sq(int x) { return x * x; }
constexpr int nine() { return 9; }

template <int N> int arrsize() { return sizeof(int[N]); }

int
main(void)
{
    /* NTTP = constexpr function call */
    if (arrsize<sq(3)>() != 36) return 1;        /* sq(3)=9, int[9]=36 */
    if (arrsize<nine()>() != 36) return 2;       /* zero-arg call */
    if (arrsize<sq(2) + 1>() != 20) return 3;    /* call inside arithmetic */
    if (arrsize<sq(sq(2))>() != 64) return 4;    /* nested call: sq(16)*4=64 */

    /* NTTP = constexpr variable (existing, kept for contrast) */
    constexpr int cv = 7;
    if (arrsize<cv>() != 28) return 5;

    /* NTTP = literal (existing) */
    if (arrsize<5>() != 20) return 6;

    return 0;
}
