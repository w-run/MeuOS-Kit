/* C++20 explicit(bool) -- basic sanity.
 * explicit(true) and explicit(false) must compile.
 * The explicit flag is stored on the constructor member. */
struct S {
    explicit(true) S(int) {}
    explicit(false) S(double) {}
    explicit S(long) {}   /* C++11 basic form */
};

int main(void) {
    S s1(42);        /* explicit(true), direct-init ok */
    S s2(3.14);      /* explicit(false), direct-init ok */
    S s3(123L);      /* explicit, direct-init ok */
    return 0;
}