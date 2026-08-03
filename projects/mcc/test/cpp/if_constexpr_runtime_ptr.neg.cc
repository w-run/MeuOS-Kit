/* C++23 P1401 companion: a non-constant runtime pointer is NOT a constant
 * expression, so `if constexpr (p)` must still be rejected. */
int f(int *p) {
    if constexpr (p)
        return 1;
    return 0;
}

int main(void) { int x; return f(&x); }
