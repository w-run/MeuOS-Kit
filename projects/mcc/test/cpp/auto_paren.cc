/* C++23 P0849: `auto(x)` decay-copy functional cast.
 *
 * `auto(expr)` is a prvalue of the decayed type of expr — arrays and
 * functions decay to pointers, and top-level cv-qualifiers are dropped,
 * exactly as `auto v = expr;` would deduce.  Returns 0 on success. */
int main(void) {
    /* scalar decay-copy */
    int x = 5;
    auto y = auto(x);
    if (y != 5) return 1;
    y = 9;               /* y is a separate copy */
    if (x != 5) return 2;

    /* array decays to a pointer */
    int arr[3] = {1, 2, 3};
    auto p = auto(arr);
    if (p[2] != 3) return 3;

    /* top-level const is dropped */
    const int cx = 7;
    auto cy = auto(cx);
    if (cy != 7) return 4;

    /* nested: auto(auto(...)) */
    auto z = auto(auto(x));
    if (z != 5) return 5;

    /* usable in expressions */
    if (auto(x + 1) != 6) return 6;

    return 0;
}
