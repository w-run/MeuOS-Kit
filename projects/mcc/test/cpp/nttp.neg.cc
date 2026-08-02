/* Negative test: non-type template parameter (NTTP) must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
template<int N> int f() { return N; }

int main(void) {
    return f<3>();   /* NTTP: not supported */
}
