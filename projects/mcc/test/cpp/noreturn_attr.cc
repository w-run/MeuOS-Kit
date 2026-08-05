/* C++ noreturn attribute consistency: all four spellings
 * (__attribute__((noreturn)), __attribute__((__noreturn__)), [[noreturn]],
 * _Noreturn) must mark a function as noreturn so that a non-void noreturn
 * function whose body does not fall off is NOT diagnosed with "control
 * reaches end of non-void function" (defect E4 GNU-attr regression).
 * Returns 0 on success. */

/* GNU attribute before the declarator. */
__attribute__((noreturn)) int gnu_before(void) { }

/* GNU attribute after the declarator. */
int gnu_after(void) __attribute__((noreturn)) { }

/* GNU double-underscore spelling. */
__attribute__((__noreturn__)) int gnu_dbl(void) { }

/* Standard C++11 spelling. */
[[noreturn]] int std_cpp(void) { }

/* Standard C11 spelling. */
_Noreturn int std_c(void) { }

/* A noreturn function call as the last statement of a non-void function
 * must not trigger the missing-return diagnostic.  The empty body of
 * `never` relies on the noreturn attribute itself (not on loop syntax)
 * to satisfy the reachability check. */
__attribute__((noreturn)) int never(void) { }
int tail_call(int x) {
    if (x)
        return 0;
    never();
}

int main(void) { return 0; }
