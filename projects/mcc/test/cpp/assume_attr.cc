/* C++23 P1774: `[[assume(expr)]]` attribute.
 *
 * The attribute is accepted as a no-op (the expression is unevaluated),
 * but the parenthesized argument form is required.  Valid in declaration,
 * function and statement positions.  Returns 0 on success. */
[[assume(1 > 0)]] int f(int x) {
    [[assume(x != 0)]];
    if (x < 0) {
        [[assume(x == -1)]];
        return -1;
    }
    return x;
}

int main(void) {
    [[assume(1 == 1)]]
    int r = f(3);
    if (r != 3) return 1;
    if (f(-1) != -1) return 2;
    return 0;
}
