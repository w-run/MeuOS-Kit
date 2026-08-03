/* Negative test: casting away const and writing through the resulting
 * pointer is undefined behavior and must be rejected (const_cast abuse).
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    const int a = 1;
    int *p = const_cast<int *>(&a);
    *p = 2;               /* modifying a const object via const_cast */
    return a;
}
