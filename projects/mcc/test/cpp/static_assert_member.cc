/* static_assert_member.cc — static_assert in class/template scope (m++).
 *
 * Covers:
 *  - a file-scope static_assert on a constexpr function call (folded)
 *  - a static_assert inside a class template, evaluated at the point of
 *    instantiation (dependent context)
 *  - run-time use of a type carrying member static_asserts
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
constexpr int
square(int n)
{
    return n * n;
}

static_assert(square(3) == 9, "constexpr call folds in static_assert");

template <typename T>
struct Checker {
    static_assert(sizeof(T) >= 1, "size is well-formed");
    static_assert(sizeof(T) <= 8, "size fits our 8-byte targets");
    int v;
};

int
main(void)
{
    Checker<int> c;
    c.v = 7;
    if (c.v != 7) return 1;
    if (square(4) != 16) return 2;
    return 0;
}
