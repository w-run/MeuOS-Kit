/* compound_literal_init.cc — compound literals / designated init (m++).
 *
 * m++ accepts C-style compound literals with designated initializers,
 * e.g. `(T){ .field = v }` and array compound literals `(T[]){ ... }`.
 * This exercises the designated-initializer form.  (The C++20 brace form
 * `T p{ .x=.., .y=.. }` is a separate, currently-unsupported feature —
 * see pending/designated_init.cc.)
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
struct P { int x; int y; };

int
main(void)
{
    P p = (P){ .x = 1, .y = 2 };
    if (p.x != 1) return 1;
    if (p.y != 2) return 2;

    int *ap = (int[]){ 5, 6, 7 };
    if (ap[0] != 5) return 3;
    if (ap[2] != 7) return 4;

    P *pp = (P[]){ (P){ .x = 3, .y = 4 }, (P){ .x = 5, .y = 6 } };
    if (pp[0].x != 3) return 5;
    if (pp[1].y != 6) return 6;

    return 0;
}
