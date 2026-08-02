/* 缺陷/限制记录：namespace 相关（4 项，m++ 2026-08-02 测试矩阵扩充）
 *
 * 1) 嵌套 namespace 中的类无法通过 Outer::Inner::Class 访问
 *      class/struct 的路径解析只支持单层；变量/函数的多级路径正常。
 *      namespace Outer { namespace Inner { class Widget {...}; } }
 *      Outer::Inner::Widget w;  → error: no class named 'Inner' in namespace 'Outer'
 *      （Outer::Inner::x 变量 / Outer::Inner::f() 函数 正常）
 *
 * 2) namespace 内「带引用参数」的自由函数调用路径解析失败
 *      namespace Geo { void fill(Point &p, int v) {...} }
 *      Geo::fill(p, 6);  → error: no class named 'fill' in namespace 'Geo'
 *      （传值参数函数 Geo::dist(Point p) 正常；类内成员引用参数正常）
 *
 * 3) namespace 内定义的类作为返回值时，返回值拷贝丢失
 *      namespace Geo { class Point { int x,y; ... }; Point mk(...){...} }
 *      Geo::Point r = Geo::mk(5);  → r.x == 0（丢失）
 *      全局函数返回 Geo 类同样丢失；传值参数方向正常（入参拷贝 OK）。
 *      （类在全局作用域时返回值拷贝正常，free_operator.cc 的 operator+）
 *
 * 4) namespace 内全局变量符号未 mangle，与全局同名变量汇编冲突
 *      int version = 1;  namespace Geo { int version = 2; }
 *      → Assembler: symbol `version' is already defined
 *      （改名避免重名后正常）
 *
 * 期望修复后：本文件 main 返回 0（所有子项按注释断言）。
 */
extern int printf(const char *, ...);

/* 子项 3：ns 类返回值 */
namespace Geo {
    class Point {
    public:
        int x, y;
        Point() { x = 0; y = 0; }
        void set(int a, int b) { x = a; y = b; }
        int sum() { return x + y; }
    };
    Point mkpoint(int xv) { Point p; p.set(xv, xv); return p; }
}

int main(void) {
    Geo::Point r = Geo::mkpoint(5);   /* 子项 3 复现点 */
    if (r.sum() != 10) return 1;      /* 当前 r.x==0 返回 1 */
    return 0;
}
