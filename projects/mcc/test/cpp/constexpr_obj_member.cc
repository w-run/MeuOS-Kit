// C++20 constexpr 聚合对象成员访问回归测试：
// constexpr 聚合对象 `constexpr P p{1, 2}` 的成员 `p.a` / `p.b` 可在
// 常量表达式（static_assert）内求值（此前报 "not an integer constant
// expression"）。

struct P {
    int a;
    int b;
};

struct S {
    int n;
};

int main() {
    constexpr P p{1, 2};
    static_assert(p.a == 1);
    static_assert(p.b == 2);
    static_assert(p.a + p.b == 3);
    if (p.a + p.b != 3) return 1;       /* 运行时访问一致 */

    constexpr S s{5};
    static_assert(s.n == 5);
    if (s.n != 5) return 2;

    /* constexpr 对象参与进一步常量算术 */
    static_assert(p.b * p.a == 2);
    static_assert(p.a < p.b);
    return 0;
}
