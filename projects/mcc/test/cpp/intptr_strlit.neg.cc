/* Negative test: initializing an int* with a string literal (const
 * char[N] -> int*) is a type error. Distinct from incompatible_assign
 * (int = string); this one pins pointer-initializer mismatch.
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    int *p = "hello";     /* const char* cannot convert to int* */
    return 0;
}
