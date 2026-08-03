/* auto_nttp.cc (SKIP — pending/)
 *
 * C++17 auto non-type template parameters `template <auto V>` are not
 * supported: m++ errors "expected 'typename' or 'class' in template
 * parameter list".  Kept as a regression marker.
 */
template <auto V>
int
get(void)
{
    return (int)V;
}

int
main(void)
{
    if (get<7>() != 7) return 1;
    if (get<'A'>() != 65) return 2;
    return 0;
}
