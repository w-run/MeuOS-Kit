/* static_assert_false.neg.cc — a static_assert with a false (and
 * non-trivially-constant) condition must be rejected at compile time.
 * m++ correctly evaluates the expression and fails the assertion with
 * "static assertion failed".
 *
 * This is a negative test: it MUST NOT compile.  Run via `check-cpp-neg`.
 */
static_assert(2 + 2 == 5, "arithmetic must hold");

int
main(void)
{
    return 0;
}
