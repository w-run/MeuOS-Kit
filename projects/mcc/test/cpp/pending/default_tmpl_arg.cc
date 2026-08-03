/* default_tmpl_arg.cc (SKIP — pending/)
 *
 * C++11 default template arguments `template <typename T = int>` are not
 * supported: m++ errors "expected ',' ... saw '='".  Kept as a
 * regression marker.
 */
template <typename T = int>
T
id(T x)
{
    return x;
}

int
main(void)
{
    if (id(5) != 5) return 1;
    if (id<double>(2.5) != 2.5) return 2;
    return 0;
}
