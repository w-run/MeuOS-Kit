/* Negative test: returning a value from a void function is ill-formed.
 * check-cpp-neg expects compilation failure.
 */
void f(void) {}
int main(void) {
    return f();           /* cannot return a value from void function */
}
