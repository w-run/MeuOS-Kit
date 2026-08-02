/* Negative test: passing an argument of incompatible type must be
 * rejected.  check-cpp-neg expects compilation failure.
 */
int f(int x) { return x; }

int main(void) {
    char *s = "hi";
    return f(s);       /* char* cannot convert to int */
}
