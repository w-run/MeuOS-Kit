/* designated_init.cc — C++20 designated initializers (C99-style
 * `.member = value` / `[index] = value` in C++), m++.
 *
 * C++20 adopted the C99 designated-initializer syntax for aggregates:
 *   - `.member = expr` selects a struct/union member
 *   - `[index] = expr` selects an array element
 *   - unlisted members/elements are value-initialized (zero)
 *   - designators may nest into subaggregates
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Point {
    int x;
    int y;
    int z;
};

struct Nested {
    Point p;
    int arr[3];
};

struct Mixed {
    int a;
    char c;
    int b;
};

int
main(void)
{
    /* partial designation: the skipped member is zero-initialized */
    Point p = {.x = 1, .z = 3};
    if (p.x != 1 || p.y != 0 || p.z != 3) return 1;

    /* full designation */
    Point q = {.x = 2, .y = 4, .z = 6};
    if (q.x != 2 || q.y != 4 || q.z != 6) return 2;

    /* nested designators: subaggregate + array index */
    Nested n = {.p = {.y = 5}, .arr = {[1] = 7}};
    if (n.p.x != 0 || n.p.y != 5 || n.p.z != 0) return 3;
    if (n.arr[0] != 0 || n.arr[1] != 7 || n.arr[2] != 0) return 4;

    /* mixing designated and implicit members (char zero-filled) */
    Mixed m = {.a = 1, .b = 3};
    if (m.a != 1 || m.c != 0 || m.b != 3) return 5;

    /* designators in a copy-list-init `= { ... }` */
    Point s = {.z = 9};
    if (s.x != 0 || s.y != 0 || s.z != 9) return 6;

    return 0;
}
