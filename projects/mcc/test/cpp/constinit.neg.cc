/* Negative test: C++20 constinit specifier must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
constinit int x = 5;   /* constinit: not supported */

int main(void) {
    return x;
}
