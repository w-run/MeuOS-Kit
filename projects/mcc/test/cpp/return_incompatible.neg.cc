/* Negative test: returning a value of incompatible type from a function
 * (const char* from an int-returning function) is a type error.
 * check-cpp-neg expects compilation failure.
 */
int f(void) {
    return "x";           /* const char* cannot convert to int */
}
int main(void) { return f(); }
