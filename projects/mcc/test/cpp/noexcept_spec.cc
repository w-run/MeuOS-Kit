/* Negative test: noexcept exception specifier must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
int f() noexcept { return 0; }   /* noexcept: not supported */

int main(void) {
    return f();
}
