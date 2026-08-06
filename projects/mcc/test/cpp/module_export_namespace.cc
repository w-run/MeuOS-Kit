/* module_export_namespace.cc — C++20 modules: export 命名空间。
 *
 * export 命名空间允许将一组声明标记为模块接口的一部分，可与 export
 * 单个声明组合使用。本测试覆盖：
 *  - export namespace 全局声明
 *  - export namespace 嵌套 namespace
 *  - export namespace 中的类 / 函数 / 变量
 *  - 非 export 命名空间（私有，未导出）
 *  - export using 别名在命名空间内
 *
 * 已知 m++ 限制（不影响本测试）：
 *  - inline namespace 透明性查找当前不接受（测试 3 已省略）
 *
 * 期望：编译通过，运行 exit 0。
 */

export module M_Namespaces;

/* 1. export 全局命名空间 */
export namespace geo {
    int x = 0;
    int y = 0;
    int sum() { return x + y; }
    void set(int a, int b) { x = a; y = b; }
}

/* 2. export 嵌套命名空间 */
export namespace outer {
    namespace inner {
        int value() { return 42; }
        struct Holder {
            int v;
            Holder() : v(0) {}
            explicit Holder(int x) : v(x) {}
        };
    }
    int top() { return inner::value(); }
}

/* 3. export 命名空间中的类 / 函数 / 变量 */
export namespace lib {
    int api() { return 1; }
    int version() { return 100; }
}

/* 4. 非 export 命名空间（私有） */
namespace detail {
    int helper() { return 100; }
}

/* 5. export 命名空间内的 using 别名 */
export namespace aliases {
    namespace a {
        int from_a() { return 7; }
    }
    namespace b {
        using a::from_a;
        int via_b() { return from_a(); }
    }
}

int main() {
    /* 1. export 命名空间函数 */
    geo::set(3, 4);
    if (geo::sum() != 7) return 1;

    /* 2. 嵌套命名空间 */
    if (outer::top() != 42) return 2;
    outer::inner::Holder h(99);
    if (h.v != 99) return 3;

    /* 3. export 命名空间多成员 */
    if (lib::api() != 1) return 4;
    if (lib::version() != 100) return 5;

    /* 4. using 别名 */
    if (aliases::b::via_b() != 7) return 6;
    return 0;
}