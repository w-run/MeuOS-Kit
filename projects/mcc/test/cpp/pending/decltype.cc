/* decltype.cc (SKIP — pending/)
 *
 * C++11 `decltype` is not supported: m++ errors "undeclared identifier:
 * decltype".  Kept as a regression marker.
 */
int
f(int x)
{
    return x + 1;
}

int
main(void)
{
    int a = 3;
    decltype(a) b = 4;
    decltype(f(0)) c = f(5);
    if (b != 4) return 1;
    if (c != 6) return 2;
    return 0;
}
