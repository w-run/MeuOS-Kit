/* generic_lambda.cc — C++14 generic lambdas (auto parameters, m++).
 *
 * A generic lambda's operator() is a function template: each call-site
 * argument type instantiates its own version.  Covers single and
 * multiple `auto` parameters, multiple distinct types per lambda, and
 * several lambdas coexisting.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int
main(void)
{
    /* single auto parameter, two value categories */
    auto twice = [](auto x) { return x * 2; };
    if (twice(3) != 6) return 1;
    if (twice(2.5) != 5.0) return 2;

    /* two auto parameters */
    auto sum = [](auto a, auto b) { return a + b; };
    if (sum(3, 4) != 7) return 3;
    if (sum(1.5, 2.5) != 4.0) return 4;

    /* several generic lambdas coexisting */
    auto inc = [](auto x) { return x + 1; };
    if (inc(5) != 6) return 5;
    if (inc(3.0) != 4.0) return 6;

    auto dbl = [](auto x) { return x + x; };
    if (dbl(2) != 4) return 7;
    if (dbl(1.5) != 3.0) return 8;

    return 0;
}
