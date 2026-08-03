/* C++23 P2360: init-statements extended to alias declarations, plus the
 * plain declaration/expression init-statement forms they build on.
 *
 * `if (using T = int; cond)`, `for (using U = int; ...)`, declaration
 * init `if (int i = 0; i)`, and expression init `if (e0; e1)` are all
 * accepted.  Returns 0 on success. */
int main(void) {
    int r = 0;

    /* alias-declaration init-statement (P2360) */
    if (using T = int; (T)7 > 0) r += 1;

    /* alias in a for-loop init-statement (P2360) */
    for (using U = int; r < 2; r = r + 1) {}

    /* declaration init-statement */
    if (int i = 5; i > 0) r += 10;

    /* expression init-statement */
    if (r = 14; r > 0) r += 100;

    /* block-scope alias usable as a type */
    using W = unsigned long;
    W big = 123;
    if (big == 123) r += 1000;

    return r == 1114 ? 0 : r;
}
