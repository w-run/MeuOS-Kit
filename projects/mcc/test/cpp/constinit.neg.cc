/* Negative test: C++20 constinit requires a constant initializer.
 * check-cpp-neg compiles this expecting failure.
 *
 * Note: `constinit` itself is now supported (see constinit.cc for the
 * positive case).  What must still be rejected is a constinit variable
 * whose initializer is not a constant expression.
 */
int f();

constinit int x = f();   /* not a constant initializer */

int main(void) {
    return x;
}
