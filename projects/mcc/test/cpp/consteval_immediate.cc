// C++20 consteval 即时调用强制回归测试：
// consteval 函数必须常量求值——非常量实参调用必须编译报错，
// 常量实参（字面量/constexpr 变量/static_assert 内）正常。

consteval int sq(int n) { return n * n; }

consteval int twice(int n) { return 2 * n; }

int main() {
    /* 常量实参：合法，且可在编译期求值 */
    int r = sq(7);
    if (r != 49) return 1;
    static_assert(sq(5) == 25);
    static_assert(twice(21) == 42);

    constexpr int k = 6;
    if (sq(k) != 36) return 2;   /* constexpr 变量实参 OK */
    return 0;
}
