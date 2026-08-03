/* using_alias.cc (SKIP — pending/)
 *
 * C++11 alias declarations `using Name = T;` are not supported: m++
 * errors "expected '::' after namespace name in using declaration".
 * Kept as a regression marker.
 */
using Int = int;

template <typename T>
using Vec = T;

int
main(void)
{
    Int x = 5;
    Vec<int> y = 7;
    if (x != 5) return 1;
    if (y != 7) return 2;
    return 0;
}
