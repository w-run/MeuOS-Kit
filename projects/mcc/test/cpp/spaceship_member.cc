// C++20 类类型三向比较 operator<=> 回归测试：
// 成员 `auto operator<=>(const T&) const` 定义与 `a <=> b` 表达式调用
// （此前 cpp_op_mangle 无 TSPACESHIP 分支报 "unsupported operator"）。

struct S {
    int x;
    S(int v) : x(v) {}
    auto operator<=>(const S& other) const { return x - other.x; }
};

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) {}
    // 非 const 成员 + 非 const 引用形参
    int operator<=>(P& other) { return (a + b) - (other.a + other.b); }
};

int main() {
    S a(1), b(2), c(2);
    if ((a <=> b) >= 0) return 1;      /* 1-2 = -1 */
    if ((b <=> c) != 0) return 2;      /* 2-2 = 0 */
    if ((b <=> a) <= 0) return 3;      /* 2-1 = 1 */

    P p(1, 2), q(4, 1);                /* sums 3 vs 5 */
    if ((p <=> q) >= 0) return 4;      /* 3-5 = -2 */
    if ((q <=> p) <= 0) return 5;      /* 5-3 = 2 */
    return 0;
}
