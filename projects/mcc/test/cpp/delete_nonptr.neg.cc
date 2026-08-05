/* Negative test: applying delete to a non-pointer object is ill-formed.
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    int x = 0;
    delete x;             /* 'delete' requires a pointer operand */
    return 0;
}
