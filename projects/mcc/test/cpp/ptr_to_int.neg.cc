/* Negative test: initializing an int from a pointer is a type error
 * (no implicit pointer -> int conversion). check-cpp-neg expects
 * compilation failure.
 */
int main(void) {
    int *p = 0;
    int x = p;            /* int* cannot convert to int */
    return x;
}
