/* constexpr_var_nonconst.neg.cc — a constexpr variable whose initializer
 * is not a constant expression must be rejected.
 */
int g = 5;   /* not constant */

constexpr int bad = g + 1;   /* g is not usable in a constant expression */

int main(void) {
    return 0;
}
