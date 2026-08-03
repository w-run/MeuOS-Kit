// D4: ctor 初始化列表标量成员落地回归测试。
// emit_base_ctors_for 只对 struct/union 成员发 ctor 调用，标量成员
// init-list 项 `: a(7)` 曾被 continue 丢弃，导致按值返回的类成员
// 内容为垃圾。修复后对命中初始化项的非 struct/union 成员发射
// `*(this+offset) = v`。

struct Foo {
    int a;
    int b;
    Foo() : a(7), b(9) {}
};

Foo make() { Foo f; return f; }

struct Bar {
    int x;
    Bar() : x(0) {}
    Bar(int v) : x(v) {}
};

int main() {
    Foo f = make();
    if (f.a != 7) return 1;
    if (f.b != 9) return 2;

    Foo g;               /* 栈上默认构造 */
    if (g.a != 7 || g.b != 9) return 3;

    Bar b(42);
    if (b.x != 42) return 4;
    return 0;
}
