// D1: const T& 形参 operator==/operator< 修复回归测试。
// 声明侧 mangle 为 `Class_operator_eqKRoVec`（const 成员 + 引用形参），
// 调用侧需 4-way 级联查找（const-K × 引用-R）并给引用形参取地址绑定。

struct Vec {
    int x;
    Vec(int v) : x(v) {}
    bool operator==(const Vec& other) const { return x == other.x; }
    bool operator<(const Vec& other) const { return x < other.x; }
    bool operator>(Vec other) { return x > other.x; }
    bool operator!=(Vec& other) { return x != other.x; }
};

// 自由函数 operator：裸名注册，值/引用形参实参绑定
bool operator<=(Vec a, Vec b) { return a.x <= b.x; }

int main() {
    Vec a(1), b(1), c(2);
    const Vec ca(1);

    if (!(a == b)) return 1;         // const 成员 operator==
    if (a == c)    return 2;
    if (!(a < c))  return 3;         // a<c 应为 true
    if (c < a)     return 4;         // c<a 应为 false
    if (a > c)     return 5;         // 非 const 值形参
    if (a != b)    return 6;         // 非 const 引用形参
    if (!(ca == b)) return 7;        // const 对象调 const 成员
    if (!(a <= c)) return 8;         // 自由函数 operator<=
    return 0;
}
