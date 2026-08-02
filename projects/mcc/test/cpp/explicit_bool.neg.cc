/* Negative test: C++20 explicit(bool) must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
struct S {
    explicit(true) S(int);   /* explicit(bool): not supported */
};

int main(void) {
    S s(1);
    return 0;
}
