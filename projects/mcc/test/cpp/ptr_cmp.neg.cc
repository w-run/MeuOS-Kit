/* Negative test: comparing pointers of unrelated pointee types (int*
 * vs char*) is ill-formed in C++ (neither is void*).
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    int *pi = 0;
    char *pc = 0;
    if (pi == pc)         /* int* and char* are not comparable */
        return 1;
    return 0;
}
