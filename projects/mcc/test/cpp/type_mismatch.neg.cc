/* Negative test: pointer arithmetic mixing a pointer and an integer must
 * be rejected as a type error.  check-cpp-neg expects compilation failure.
 */
int main(void) {
    int *p = 0;
    return p + 1;      /* pointer + int */
}
