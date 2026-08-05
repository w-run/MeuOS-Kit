/* C++20 lambda template parameters -- []<typename T>(T x) { ... }.
 * The template parameter list is parsed and emitted as
 * `template<...> operator()(...)` in the synthesized closure class. */
int main(void) {
    int r = 0;
    /* basic lambda with explicit template parameter */
    auto f = []<typename T>(T x) { return (int)x + 1; };
    r = f(41);
    if (r != 42) return 1;

    /* template parameter used in body */
    auto g = []<typename T>(T a, T b) { return a > b ? a : b; };
    if (g(3, 7) != 7) return 2;
    if (g(9, 2) != 9) return 3;

    /* template + by-value capture */
    int cap = 10;
    auto h = [cap]<typename T>(T x) { return (int)x + cap; };
    if (h(5) != 15) return 4;

    /* generic lambda with auto param (should still work) */
    auto gen = [](auto x) { return (int)x + 1; };
    if (gen(41) != 42) return 5;

    return 0;
}