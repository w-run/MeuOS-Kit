/* Negative test: initializing/assigning an int into a pointer is a type
 * error (no implicit int -> pointer conversion). check-cpp-neg expects
 * compilation failure.
 */
int main(void) {
    int *p;
    p = 1;                /* int cannot convert to int* */
    return 0;
}
