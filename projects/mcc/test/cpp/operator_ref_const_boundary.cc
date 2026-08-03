// D1/T02 边界：`const A&&` 形参 + 模板推导边界。
//
// 声明侧引用编码对 rvalue 引用与 lvalue 引用不同（R / V），级联查找必须
// 两态都试；`const A&&` 还叠加 const-K 一维，构成 4-way 的另一角。
// 模板侧检查注入类名 template-id（`Pair<T>`）在实例化替换后仍指向本实例，
// 且运算符与嵌套模板参数（`Pair<T>` 成员）共存时不误判。

struct A {
    int x;
    A(int v) : x(v) {}
    // const 成员 + const rvalue 引用形参：mangle 同时带 K 与 rvalue 引用码
    bool operator==(const A&& o) const { return x == o.x; }
    // 非 const 成员 + rvalue 引用形参（无 K）
    bool operator<(A&& o) { return x < o.x; }
};

// 自由函数 const rvalue 引用形参
bool operator>(const A& a, const A&& b) { return a.x > b.x; }

// 模板 + 注入类名 template-id 形参，且同时存在 const T& 与值形参重载
template <typename T>
struct Pair {
    T a;
    T b;
    Pair(T x, T y) : a(x), b(y) {}
    bool operator==(const Pair<T>& o) const { return a == o.a && b == o.b; }
    bool operator!=(const Pair<T>& o) const { return !(a == o.a && b == o.b); }
};

// 注：模板成员函数/运算符的「rvalue 引用」形参（如 `operator<(const Pair<T>&&)`
// 或普通成员 `void f(P&&)`）当前无法解析 —— 这是 m++ 预存在的独立缺陷
// （在类模板内任一成员使用 `T&&` 形参即可复现，与本 const-K/引用-R 级联
// 决议无关），故不混入本用例；`const A&&` 覆盖由下方非模板的 A 类提供。

// 注：形参为类模板实例 const 引用的「自由函数模板」（如
// `template<class T> bool f(const Pair<T>&)`）此处未覆盖 —— m++ 当前对该
// 形态的形参推导有预存在缺陷（不含任何 operator 亦可复现），与本运算符
// 决议路径无关，故不混入本用例。

int main() {
    A a(1);
    const A ca(2);

    if (!(a == A(1))) return 1;      // const 成员 + const A&&
    if (a == A(2))    return 2;
    if (!(a < A(5)))  return 3;      // 非 const 成员 + A&&
    if (a < A(0))     return 4;
    if (!(ca > A(1))) return 5;      // 自由函数 const A& / const A&&
    if (ca > A(9))    return 6;

    Pair<int> p(1, 2), q(1, 2), r(3, 4);
    if (!(p == q)) return 7;         // 模板 const Pair<T>&
    if (p == r)    return 8;
    if (!(p != r)) return 9;
    if (p != q)    return 10;

    return 0;
}
