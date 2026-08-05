// D1/T02: 类类型运算符 `const T&` 形参重载决议回归测试。
//
// 声明侧 mangle 形如 `Vec_operator_eqKRoVec`（const 成员追加 K，引用形参
// 前缀 R）。调用侧需 4-way 级联查找（const-K × 引用-R）并给引用形参取地址
// 绑定，否则 operator==/operator< 整体查不到（D1）。
//
// 覆盖三档形参（`const T&` / `T&` / `T`）× 三种宿主（成员 / 自由函数 /
// 类模板），外加同一运算符的多重载共存。

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
bool operator>=(const Vec& a, const Vec& b) { return a.x >= b.x; }

// 类模板内的运算符：形参写成注入类名的 template-id `Box<T>`，实例化
// 替换后必须仍解析为本实例类型。
template <typename T>
struct Box {
    T v;
    Box(T a) : v(a) {}
    bool operator==(const Box<T>& o) const { return v == o.v; }  // const T&
    bool operator<(const Box<T>& o) const { return v < o.v; }
    bool operator>(Box<T> o) const { return v > o.v; }           // 值形参
};

// 多重载共存：同名运算符在不同类上各自决议
struct Tag {
    int k;
    Tag(int a) : k(a) {}
    bool operator==(const Tag& o) const { return k == o.k; }
};

int main() {
    Vec a(1), b(1), c(2);
    const Vec ca(1);

    if (!(a == b)) return 1;         // const 成员 + const T&
    if (a == c)    return 2;
    if (!(a < c))  return 3;
    if (c < a)     return 4;
    if (a > c)     return 5;         // 非 const 值形参
    if (a != b)    return 6;         // 非 const 引用形参
    if (!(ca == b)) return 7;        // const 对象调 const 成员
    if (!(a <= c)) return 8;         // 自由函数值形参
    if (!(c >= a)) return 9;         // 自由函数 const T&
    if (a >= c)    return 10;

    // 模板形参档位
    Box<int> p(1), q(1), r(2);
    if (!(p == q)) return 11;        // 模板 const Box<T>&
    if (p == r)    return 12;
    if (!(p < r))  return 13;
    if (r < p)     return 14;
    if (!(r > p))  return 15;        // 模板值形参
    if (p > r)     return 16;

    // 注：第二个实例化（如 Box<long>）此处未覆盖 —— m++ 当前对同一类模板
    // 的第二个实例化无法决议其构造函数（预存在缺陷，与本运算符路径无关，
    // 无 operator 参与也可复现），故不在本用例中混入。

    // 多重载共存
    Tag g(3), h(3), i2(4);
    if (!(g == h)) return 20;
    if (g == i2)   return 21;

    return 0;
}
