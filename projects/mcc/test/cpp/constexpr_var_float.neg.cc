/* constexpr_var_float.neg.cc — C++ constexpr variables are limited to
 * integer constant initializers in the phase-1 subset; a floating-point
 * constexpr variable must be rejected.
 */
constexpr double d = 1.5;

int main(void) {
    return 0;
}
