/* if_constexpr_nonconst.neg.cc — `if constexpr` requires a constant
 * condition.  A runtime value is ill-formed and must be rejected by the
 * compiler.  m++ correctly diagnoses this with
 * "if constexpr condition is not a constant expression".
 *
 * This is a negative test: it MUST NOT compile.  Run via `check-cpp-neg`.
 */
int
main(void)
{
    int n = 1;
    if constexpr (n) {
        return 0;
    }
    return 0;
}
