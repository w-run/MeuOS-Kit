/* Positive test: C++11 thread_local storage class.
 * check-cpp-func compiles and runs this expecting exit status 0.
 *
 * thread_local marks a variable with thread-local (TLS) storage duration.
 * Combined with static or extern for linkage control.
 */
thread_local int tls_var = 42;

static thread_local int tls_static = 7;

int main(void) {
    if (tls_var != 42)
        return 1;
    if (tls_static != 7)
        return 2;
    tls_var = 99;
    if (tls_var != 99)
        return 3;
    return 0;
}