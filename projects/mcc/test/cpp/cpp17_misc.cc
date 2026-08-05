/* cpp17_misc.cc — C++17 structured bindings and inline variables (m++).
 *
 * Structured binding: `auto [x, y] = p;` creates a hidden object
 * initialized from the initializer and binds each name to a copy of the
 * corresponding member (value bindings; mutating a binding does not
 * touch the original object).
 *
 * Inline variables: `inline static int count = 0;` inside a class is a
 * definition (zero-initialized when no initializer is given), and a
 * file-scope `inline int g = 42;` is usable like any global.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Point { int x; int y; };
struct Triple { int a; int b; int c; };

struct Counter {
    inline static int count = 0;
    inline static int total;
};
inline int global_inline = 42;

int
main(void)
{
    /* structured binding: two members */
    Point p = {3, 4};
    auto [x, y] = p;
    if (x != 3) return 1;
    if (y != 4) return 2;
    x = 100;               /* value binding: original unchanged */
    if (p.x != 3) return 3;

    /* structured binding: three members */
    Triple t = {1, 2, 3};
    auto [ta, tb, tc] = t;
    if (ta + tb + tc != 6) return 4;

    /* structured binding re-bound from the same object */
    auto [a, b] = p;
    if (a != 3 || b != 4) return 5;

    /* inline static members: initialized and zero-initialized */
    Counter::count = 5;
    Counter::total = 7;
    if (Counter::count != 5) return 6;
    if (Counter::total != 7) return 7;

    /* file-scope inline variable */
    if (global_inline != 42) return 8;
    global_inline = 100;
    if (global_inline != 100) return 9;

    return 0;
}
