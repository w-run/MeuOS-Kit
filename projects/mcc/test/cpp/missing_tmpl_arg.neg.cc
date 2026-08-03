/* Negative test: using a class template without supplying its required
 * type argument is ill-formed. check-cpp-neg expects compilation failure.
 */
template <typename T> struct S { T v; };
int main(void) {
    S s;                  /* missing template argument for 'S' */
    return 0;
}
