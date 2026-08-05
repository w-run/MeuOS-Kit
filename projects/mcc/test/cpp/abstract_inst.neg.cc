/* Negative test: instantiating an abstract class (one with a pure
 * virtual function) is ill-formed. check-cpp-neg expects compilation
 * failure.
 */
struct A { virtual void f() = 0; };
int main(void) {
    A a;                  /* 'A' is abstract */
    return 0;
}
