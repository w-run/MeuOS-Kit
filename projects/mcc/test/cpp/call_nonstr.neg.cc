/* Negative test: calling an object that is not a function (here an int)
 * is a type error. check-cpp-neg expects compilation failure.
 */
int main(void) {
    int x = 0;
    x();                  /* 'x' is not callable */
    return 0;
}
