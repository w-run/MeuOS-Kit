/* uniform_init.cc (SKIP — pending/)
 *
 * C++11 uniform initialization `T x{...}` is not supported: m++ errors
 * "expected ',' or ';' after declarator, saw '{'".  Kept as a regression
 * marker.  (C-style `T x = {...}` aggregate init IS supported — see
 * braced_init_array.cc.)
 */
struct P { int x; int y; };

int
main(void)
{
    P p{3, 4};
    if (p.x != 3) return 1;
    if (p.y != 4) return 2;

    int a[]{1, 2, 3};
    if (a[2] != 3) return 3;

    return 0;
}
