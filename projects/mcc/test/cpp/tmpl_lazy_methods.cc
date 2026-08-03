// D2: 类模板实例化惰性方法体回归测试。
// 模板实例化时未使用成员函数不应急切解析——`bad()` 体对 int 非法，
// 但从未被调用，不得使整个 Box<int> 实例化失败。
// 使用方法（get/ctor）必须在调用点按需解析并正确工作。

template <typename T>
struct Box {
    T val;
    Box(T v) : val(v) {}
    T get() { return val; }
    void bad() { val.nonexistent(); }   // 未使用；对 int 非法
};

template <typename T>
struct Two {
    T a, b;
    Two(T x, T y) : a(x), b(y) {}
    T sum() { return a + b; }
};

int main() {
    Box<int> b(42);
    if (b.get() != 42) return 1;

    Box<char> c((char)7);
    if (c.get() != 7) return 2;

    Two<int> t(10, 32);
    if (t.sum() != 42) return 3;

    Two<double> d(1.5, 2.25);
    if (d.sum() != 3.75) return 4;

    return 0;
}
