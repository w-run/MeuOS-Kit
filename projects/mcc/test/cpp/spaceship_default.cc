/* spaceship_default.cc — C++20 defaulted three-way comparison:
 * `auto operator<=>(const T&) const = default;` synthesizes a body that
 * compares every non-static data member in declaration order, and the
 * relational/equality operators rewrite to `(a <=> b) op 0` when no
 * direct operator exists.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Vec {
    int x; int y;
    auto operator<=>(const Vec&) const = default;
};

int
main(void)
{
    Vec a{1, 2}, b{1, 3}, c{1, 2};
    if (a < b) {} else return 1;      /* 1<3 on second member */
    if (a == c) {} else return 2;     /* equal members */
    if (a != b) {} else return 3;     /* different second member */
    if (b > a) {} else return 4;      /* 3>2 on second member */
    if (a <= c) {} else return 5;     /* equal => <= holds */
    if (a >= c) {} else return 6;     /* equal => >= holds */
    if (b <= a) return 7;             /* 1,3 <= 1,2 is false */
    if (b >= c) {} else return 8;     /* 1,3 >= 1,2 is true */
    return 0;
}
