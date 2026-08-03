/* designated_init.cc (SKIP — pending/)
 *
 * C++20 designated initializers `T x{ .a = .., .b = .. }` are not
 * supported (m++ rejects the brace form with "saw '{'").  Kept as a
 * regression marker.  The C compound-literal form `(T){ .a = .. }` IS
 * supported — see compound_literal_init.cc.
 */
struct P { int x; int y; };

int
main(void)
{
    P p{ .x = 1, .y = 2 };
    if (p.x != 1) return 1;
    if (p.y != 2) return 2;
    return 0;
}
