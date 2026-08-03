/* Negative test: member access through a non-class type (int) is a type
 * error. check-cpp-neg expects compilation failure.
 */
int main(void) {
    int x = 0;
    x.foo;                /* 'int' has no members */
    return 0;
}
