/* Negative test: referencing an undeclared identifier is ill-formed.
 * check-cpp-neg expects compilation failure.
 */
int main(void) {
    return nope;          /* 'nope' was never declared */
}
