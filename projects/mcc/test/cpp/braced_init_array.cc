/* braced_init_array.cc — C++11 aggregate (braced) initialization (m++).
 *
 * Covers C-style brace initialization of:
 *  - a plain int array  `int a[N] = { ... }`
 *  - an array of structs `P arr[2] = { {..}, {..} }`
 * and run-time access through both.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
struct P { int x; int y; };

int
main(void)
{
    int a[4] = { 1, 2, 3, 4 };
    if (a[0] != 1) return 1;
    if (a[3] != 4) return 2;

    P arr[2] = { { 10, 20 }, { 30, 40 } };
    if (arr[0].y != 20) return 3;
    if (arr[1].x != 30) return 4;

    /* partial initializer fills the rest with zero */
    int b[3] = { 7 };
    if (b[0] != 7) return 5;
    if (b[2] != 0) return 6;

    return 0;
}
