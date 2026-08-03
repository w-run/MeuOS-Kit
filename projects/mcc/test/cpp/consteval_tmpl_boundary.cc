/* consteval_tmpl_boundary.cc — C++20 consteval in templates (grace).
 *
 * Covers the template side of the immediate-invocation boundary:
 *  - consteval function templates (`template<typename T> consteval T twice`)
 *    fold in constant contexts and error on runtime arguments
 *  - consteval called inside a constexpr function / template with a
 *    constant argument folds (`constexpr int g() { return sq(5); }`)
 *  - consteval + dependent-type NTTP intersection
 *    (`template<typename T, T N> constexpr T f() { return sq(N); }`)
 *  - a runtime (non-constant) argument to a consteval call is a compile
 *    error even in a constexpr function (immediate invocation must be a
 *    constant expression); see consteval_nonconst.neg.cc
 *
 * Returns 0 on success. */
consteval int sq(int n) { return n * n; }

template<typename T>
consteval T twice(T x) { return x * 2; }

template<int N>
constexpr int from_nttp() { return sq(N); }

template<typename T, T N>
constexpr T dep_nttp() { return (T)sq((int)N); }

static_assert(twice(3) == 6, "consteval fn template int");
static_assert(twice(3L) == 6L, "consteval fn template long");
static_assert(sq(5) == 25, "consteval constant arg");
static_assert(from_nttp<4>() == 16, "consteval in NTTP template");
static_assert(dep_nttp<int, 5>() == 25, "consteval + dependent NTTP");

int main(void) {
    constexpr int a = sq(3);          /* constant arg folds */
    if (a != 9) return 1;
    if (from_nttp<6>() != 36) return 2;
    if (twice(7) != 14) return 3;
    return 0;
}
