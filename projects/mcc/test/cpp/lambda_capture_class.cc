/* lambda_capture_class.cc — lambda 按值捕获类对象走拷贝构造函数
 * （缺陷 S 回归，m++ end-to-end）。
 *
 * 缺陷 S（2026-08-03 修复）：lambda 降级为匿名闭包类时，捕获成员由
 * ctor 体里的赋值 `cap = __c;` 初始化，对类类型是逐字节位拷贝，
 * 自定义拷贝 ctor 从不被调用，构造副作用（引用计数、深拷贝、日志）
 * 全部丢失。修复后带用户 ctor 的类捕获改走 ctor 初始化列表
 * `: cap(__c)`，由成员初始化路径按重载解析选择拷贝/移动 ctor。
 *
 * 对照：标量捕获（见 lambda.cc）与 POD 捕获仍是位拷贝，语义正确。
 *
 * 每个检查返回不同退出码；退出 0 = 全部通过。
 */
int copies = 0;

class C {
public:
    int m;
    C() { m = 1; }
    C(C &o) { m = o.m + 100; copies++; }
};

/* 无用户 ctor 的 POD：捕获保持位拷贝 */
struct Pod {
    int a;
    int b;
};

int
main(void)
{
    C c;
    auto f = [c] { return c.m; };

    if (copies != 1) return 1;   /* 捕获时必须调用一次拷贝 ctor */
    if (f() != 101) return 2;    /* 捕获副本的 m 应带 +100 */
    if (c.m != 1) return 3;      /* 原对象不受影响 */

    /* 捕获后修改原对象不影响已捕获的副本（按值快照） */
    c.m = 50;
    if (f() != 101) return 4;

    /* POD 捕获走位拷贝，不需要 ctor */
    Pod p;
    p.a = 3;
    p.b = 4;
    auto g = [p] { return p.a * 10 + p.b; };
    if (g() != 34) return 5;
    if (copies != 1) return 6;   /* POD 捕获不产生额外拷贝 ctor 调用 */

    /* 类捕获与标量捕获混合：类走 ctor，标量走赋值 */
    int k = 7;
    C c2;
    auto h = [c2, k] { return c2.m + k; };
    if (copies != 2) return 7;
    if (h() != 108) return 8;    /* 101 + 7 */

    return 0;
}
