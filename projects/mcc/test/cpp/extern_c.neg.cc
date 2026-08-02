/* Negative test: extern "C" linkage specifier must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
extern "C" int g(void);   /* extern "C": not supported */

int main(void) {
    return g();
}
