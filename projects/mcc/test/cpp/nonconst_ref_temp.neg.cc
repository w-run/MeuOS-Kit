/* Negative test: binding a non-const reference to a temporary (rvalue)
 * is ill-formed. check-cpp-neg expects compilation failure.
 */
int main(void) {
    int &r = 5;           /* non-const lvalue ref cannot bind rvalue */
    return r;
}
