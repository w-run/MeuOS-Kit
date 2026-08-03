// C++20 NTTP（非类型模板参数）回归测试：
// `template<int N>`（固定类型值参数）、`template<auto V>`（推导值参数）、
// 类模板 NTTP（数组维度）、constexpr 折叠（static_assert）。

template <int N>
constexpr int sz() { return N; }

template <int N>
struct Arr {
    int data[N];
};

template <auto V>
constexpr int av() { return V; }

int main() {
    /* 函数模板 NTTP：显式值实参，运行时 + 编译期折叠 */
    int x = sz<42>();
    if (x != 42) return 1;
    if (sz<7>() != 7) return 2;
    static_assert(sz<7>() == 7);
    static_assert(sz<42>() == 42);
    static_assert(av<99>() == 99);

    /* 类模板 NTTP：数组维度 */
    Arr<3> a;
    a.data[0] = 7;
    a.data[2] = 9;
    if (a.data[0] + a.data[2] != 16) return 3;
    Arr<5> b;
    b.data[4] = 11;
    if (b.data[4] != 11) return 4;

    /* auto NTTP 运行时 */
    if (av<100>() != 100) return 5;
    return 0;
}
