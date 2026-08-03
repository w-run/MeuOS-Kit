/* Negative test: subscripting a non-array, non-pointer type (int) is a
 * type error. check-cpp-neg expects compilation failure.
 */
int main(void) {
    int x = 0;
    x[0];                 /* 'int' is not subscriptable */
    return 0;
}
