/* Negative test: modifying a const object is ill-formed.
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    const int c = 5;
    c = 6;                /* assignment to const object */
    return c;
}
