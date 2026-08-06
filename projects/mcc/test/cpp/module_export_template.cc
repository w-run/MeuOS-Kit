/* module_export_template.cc — C++20 modules: export 模板函数 / 模板类。
 *
 * module_basic.cc 覆盖 export 函数 / export block / nested export。
 * 本测试扩展到模板：
 *  - export template 函数（多种类型实例化）
 *  - export template 类（成员函数 export）
 *
 * 已知 m++ 限制（不影响本测试）：
 *  - `template int identity<int>(int);` 显式实例化声明语法不接受
 *  - `template <> struct Box<...> {...}` 特化语法不接受
 *  - 模板类的成员 typedef 访问（alias_traits<T>::type）不接受
 *
 * 期望：编译通过，运行 exit 0（所有 check 通过）。
 */

export module M_Templates;

/* export 模板函数 */
export template <typename T>
T identity(T x) { return x; }

/* export 模板类（带成员函数） */
export template <typename T>
struct Box {
    T value;
    Box() : value() {}
    explicit Box(T v) : value(v) {}
    T get() const { return value; }
    void set(T v) { value = v; }
};

int main() {
    /* 1. export 模板函数（int + double） */
    if (identity(42) != 42) return 1;
    if (identity(3.14) < 3.13 || identity(3.14) > 3.15) return 1;

    /* 2. export 模板类（基础 + 多种类型） */
    Box<int> b1(7);
    if (b1.get() != 7) return 2;
    b1.set(11);
    if (b1.get() != 11) return 2;

    /* 3. 模板不同实例化（double） */
    Box<double> bd(3.5);
    if (bd.get() < 3.4 || bd.get() > 3.6) return 3;

    /* 4. 模板不同实例化（int + 多次实例化） */
    Box<int> b2(100);
    if (b2.get() != 100) return 4;
    return 0;
}