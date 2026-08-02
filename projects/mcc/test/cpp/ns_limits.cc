/* ns_limits.cc — namespace scope limits (m++ end-to-end).
 *
 * Covers four namespace-related cases:
 *  E1) nested-namespace class paths: `Outer::Inner::Widget`
 *  E2) free functions in a namespace with reference parameters:
 *      `Geo2::fill(Point &p, int v)`
 *  E3) a class-typed return value from a namespace function
 *      (copy-initialization must not be clobbered by a default ctor)
 *  E4) a namespace-scope variable/function does not collide with a
 *      same-named global (`Geo::version` vs `version`, `Geo2::dist`).
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

/* E4 复现：ns 变量与全局同名 */
int version = 1;
namespace Geo {
    int version = 2;
}

/* E2 复现：全局 Point 类 + ns 内引用参数函数 */
class Point {
public:
    int x, y;
    Point() { x = 0; y = 0; }
    void set(int a, int b) { x = a; y = b; }
    int sum() { return x + y; }
};

namespace Geo2 {
    void fill(Point &p, int v) { p.set(v, v); }
    int dist(Point p) { return p.x * 2; }
}

/* E3 复现：ns 类返回值 */
namespace Geo3 {
    class Pt {
    public:
        int x, y;
        Pt() { x = 0; y = 0; }
        void set(int a, int b) { x = a; y = b; }
        int sum() { return x + y; }
    };
    Pt mkpoint(int xv) { Pt p; p.set(xv, xv); return p; }
}

/* E1 复现：嵌套 ns 类 */
namespace Outer {
    namespace Inner {
        class Widget {
        public:
            int w;
            Widget() { w = 0; }
            void set(int v) { w = v; }
            int get() { return w; }
        };
    }
    int top = 5;
}

int main(void) {
    /* E4: ns 变量与全局变量各自独立 */
    if (version != 1) return 1;       /* global version */
    if (Geo::version != 2) return 2;  /* Geo::version */
    Geo::version = 3;
    if (Geo::version != 3) return 3;
    if (version != 1) return 4;       /* global unchanged */

    /* E2: ns 内引用参数自由函数调用 */
    Point p;
    p.set(1, 2);
    Geo2::fill(p, 6);
    if (p.sum() != 12) return 5;     /* ref param write lands */
    Geo2::fill(p, 7);
    if (p.sum() != 14) return 6;
    if (Geo2::dist(p) != 14) return 7; /* pass-by-value still works */

    /* E3: ns 内类返回值拷贝 */
    Geo3::Pt r = Geo3::mkpoint(5);
    if (r.sum() != 10) return 8;

    /* E1: 嵌套 ns 类路径 */
    Outer::Inner::Widget w;
    w.set(42);
    if (w.get() != 42) return 9;
    if (Outer::top != 5) return 10;

    printf("ns_limits: all 4 items passed\n");
    return 0;
}
