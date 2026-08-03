/* Negative test: defining the same function twice is a redefinition and
 * ill-formed. check-cpp-neg expects compilation failure.
 */
int f(void) { return 1; }
int f(void) { return 2; }
int main(void) { return f(); }
