/* Negative test: a function whose body returns a value of incompatible
 * type must be rejected.  check-cpp-neg expects compilation failure.
 */
int f(int x) { return "string"; }   /* int function returning char* */

int main(void) { return f(1); }
