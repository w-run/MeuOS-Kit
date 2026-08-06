/* coroutine_coreturn.cc — Phase 1: co_return lowered as direct return.
 *
 * m++ implements co_return as a direct function return (no promise
 * protocol, no coroutine frame).  This is the simplest coroutine form:
 * just a return expression.
 *
 * Tests:
 *  - co_return with integer expression
 *  - co_return without expression (void)
 *  - multiple co_return in different functions
 *  - co_return with complex expression
 *  - co_return in nested scope
 *
 * Each check returns a different exit code; exit 0 = all passed.
 */

int square(int x) {
    co_return x * x;
}

int sum(int a, int b) {
    co_return a + b;
}

void empty_return() {
    co_return;
}

int compute(int v) {
    if (v > 0)
        co_return v * 2;
    co_return -v;
}

int
main(void)
{
    /* 1. Basic co_return with expression */
    if (square(5) != 25) return 1;

    /* 2. co_return with two arguments */
    if (sum(10, 20) != 30) return 2;

    /* 3. co_return without expression (void function) */
    empty_return();

    /* 4. co_return with complex expression */
    if (square(7) != 49) return 4;
    if (square(-3) != 9) return 4;

    /* 5. co_return in conditional path */
    if (compute(5) != 10) return 5;
    if (compute(-3) != 3) return 5;

    /* 6. Multiple functions with co_return */
    if (sum(-5, 5) != 0) return 6;
    if (sum(100, 200) != 300) return 6;

    return 0;
}