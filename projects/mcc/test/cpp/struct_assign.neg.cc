/* Negative test: copy-assigning between objects of unrelated struct types
 * is a type error (no built-in operator= between A and B).
 * check-cpp-neg expects compilation failure.
 */
struct A {};
struct B {};
int main(void) {
    A a;
    B b;
    a = b;                /* A and B are unrelated types */
    return 0;
}
