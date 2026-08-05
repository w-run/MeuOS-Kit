/* Negative test: applying operator+ to two objects of a class type that
 * defines no matching operator is ill-formed. check-cpp-neg expects
 * compilation failure.
 */
struct A {};
int main(void) {
    A x, y;
    A z = x + y;          /* no operator+ for A */
    return 0;
}
