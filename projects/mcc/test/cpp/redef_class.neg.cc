/* Negative test: redefining a class type in the same scope is
 * ill-formed. check-cpp-neg expects compilation failure.
 */
struct A {};
struct A {};
int main(void) { return 0; }
