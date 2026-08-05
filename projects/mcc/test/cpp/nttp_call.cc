/* nttp_call.cc — a constexpr function call as a non-type template
 * argument.
 *
 * Regression for cpp_tmpl_explicit_parse: an outer template's explicit
 * argument list is parsed while the template name is still pending on
 * g_cpp_tmpl_stack, so an inner call in an argument (`arrsize<sq(3)>()`)
 * had its `(` lowered by cpp_tmpl_instantiate — which popped the *outer*
 * pending name and tried to instantiate `arrsize` with `sq(3)` as its
 * argument list ("too many arguments").  The pending depth must be hidden
 * while parsing the explicit-argument expressions and restored afterwards.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

constexpr int sq(int x)   { return x * x; }
constexpr int nine()      { return 9; }

template<int N> int arrsize() { return sizeof(int[N]); }

int
main(void)
{
    /* NTTP = a constexpr function call */
    if (arrsize<sq(3)>() != 36) return 1;      /* sq(3) = 9  -> int[9]  */
    if (arrsize<sq(2)>() != 16) return 2;      /* sq(2) = 4  -> int[4]  */
    if (arrsize<nine()>() != 36) return 3;     /* 0-arg call  -> int[9]  */

    /* call combined into a larger constant expression */
    if (arrsize<sq(3) + 1>() != 40) return 4;  /* 9+1 = 10   -> int[10] */
    if (arrsize<(sq(3))>() != 36) return 5;    /* parens      -> int[9] */

    /* constexpr object/variable derived from a call used as a NTTP */
    constexpr int c = sq(6);                    /* 36 */
    if (arrsize<c>() != 144) return 6;          /* int[36]     */

    return 0;
}
