/* enum_class.cc (SKIP — pending/)
 *
 * C++11 scoped enumerations `enum class E { ... }` are not supported:
 * m++ errors "expected ',' or ';' after declarator, saw '{'".  Kept as a
 * regression marker.
 */
enum class Color { Red, Green, Blue };

int
main(void)
{
    Color c = Color::Green;
    if ((int)c != 1) return 1;
    if (c == Color::Red) return 2;
    return 0;
}
