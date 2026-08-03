/* Negative test: redefining a local variable in the same block scope is
 * ill-formed. check-cpp-neg expects compilation failure.
 */
int main(void) {
    int a;
    int a;                /* redeclaration of 'a' in same scope */
    return a;
}
