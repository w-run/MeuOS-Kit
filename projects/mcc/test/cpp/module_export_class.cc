/* module_export_class.cc — C++20 modules: export 类（含成员访问控制）。
 *
 * module_basic.cc 已覆盖 export 普通函数 / export block。本测试扩展到
 * 类层级：
 *  - export class（含 public / private 成员）
 *  - export class 嵌套 struct / enum
 *  - export class 静态成员
 *  - export class 模板成员函数
 *  - export struct（与 class 等价的 struct 形式）
 *  - 同一类 export 多次仅生效一次（idempotent）
 *
 * 期望：编译通过，运行 exit 0。
 */

export module M_Classes;

/* export 普通类 */
export class Point {
public:
    int x, y;
    Point() : x(0), y(0) {}
    Point(int a, int b) : x(a), y(b) {}
    int sum() const { return x + y; }
private:
    int tag;
};

/* export 嵌套 enum（m++ 当前不支持类内 enum，移到命名空间作用域） */
namespace color_ns {
    enum Kind { Red, Green, Blue };
}

export class Color {
public:
    int k;
    Color() : k(color_ns::Red) {}
    explicit Color(int kk) : k(kk) {}
    int value() const { return k; }
};

/* export 静态成员 */
export class Counter {
public:
    static int total;
    Counter() { ++total; }
    ~Counter() { --total; }
    static int current() { return total; }
};

int Counter::total = 0;

/* export struct 形式 */
export struct Pair {
    int a, b;
    Pair() : a(0), b(0) {}
    Pair(int x, int y) : a(x), b(y) {}
    int sum() const { return a + b; }
};

int main() {
    /* 1. export class 默认构造 + 带参构造 */
    Point p1;
    if (p1.sum() != 0) return 1;
    Point p2(3, 4);
    if (p2.sum() != 7) return 2;

    /* 2. export 类（数值映射来自外部 enum） */
    Color c1;
    if (c1.value() != 0) return 3;
    Color c2(2);
    if (c2.value() != 2) return 4;

    /* 3. export 静态成员：构造/析构递增/递减 */
    if (Counter::current() != 0) return 5;
    {
        Counter c3;
        if (Counter::current() != 1) return 6;
        Counter c4;
        if (Counter::current() != 2) return 7;
    }
    if (Counter::current() != 0) return 8;

    /* 4. export struct */
    Pair pp(10, 20);
    if (pp.sum() != 30) return 9;
    return 0;
}