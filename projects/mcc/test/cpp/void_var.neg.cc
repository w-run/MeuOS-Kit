/* Negative test: declaring a variable of type void is ill-formed.
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    void x;               /* 'void' is an incomplete/invalid object type */
    return 0;
}
