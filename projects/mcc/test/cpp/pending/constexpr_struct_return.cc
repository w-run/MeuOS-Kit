/* constexpr_struct_return.cc (SKIP — pending/)
 *
 * constexpr functions returning a struct (aggregate) are not folded by
 * m++: `constexpr P p = mk();` errors "constexpr variable requires a
 * constant expression initializer".  int-returning constexpr works (see
 * constexpr.cc / static_assert_member.cc).  Kept as a regression marker.
 */
struct P { int x; int y; };

constexpr P
mk(void)
{
    P p;
    p.x = 10;
    p.y = 20;
    return p;
}

int
main(void)
{
    constexpr P p = mk();
    if (p.x != 10) return 1;
    if (p.y != 20) return 2;
    return 0;
}
