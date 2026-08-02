/* 缺陷记录：lambda 按值捕获类对象不调用拷贝构造函数（defect S）
 *
 * 触发条件（worktree-mxx-work，2026-08-03 测试矩阵扩充中发现）：
 *   带自定义拷贝 ctor 的类被 lambda 按值捕获时，ctor 不被调用，
 *   捕获走的是逐字节位拷贝：
 *     class C { C(C &o) { m = o.m + 100; copies++; } };
 *     C c;  auto f = [c] { return c.m; };
 *     // 实测 copies==0、f()==1；期望 copies==1、f()==101
 *
 * 对照：
 *   - 标量捕获（int 等）语义正确（见 test/cpp/lambda.cc，全绿）。
 *   - 普通按值传参会正确选择拷贝/移动 ctor（见 test/cpp/move_semantics.cc），
 *     说明缺陷局限在 lambda 捕获的降级路径，而非通用参数传递。
 *
 * 影响面：任何带自定义拷贝 ctor 的类被按值捕获时，构造副作用
 *       （引用计数、深拷贝、日志）全部丢失。
 *
 * 疑似根因：lambda 降级为匿名类时，捕获成员的初始化用了原始内存拷贝
 *       而非 ctor 调用；应按成员类型走重载解析（与按值传参同一路径）。
 *
 * 期望修复后：本文件 main 返回 0。
 *
 * 注意：本文件在 pending/ 目录，不接入 check-cpp-* glob（避免 CI 红）。
 *       修复后把断言移入 lambda_capture_boundary.cc 并删除本文件。
 */
int copies = 0;

class C {
public:
    int m;
    C() { m = 1; }
    C(C &o) { m = o.m + 100; copies++; }
};

int
main(void)
{
    C c;
    auto f = [c] { return c.m; };

    if (copies != 1) return 1;   /* 捕获时必须调用一次拷贝 ctor */
    if (f() != 101) return 2;    /* 捕获副本的 m 应带 +100 */
    if (c.m != 1) return 3;      /* 原对象不受影响 */
    return 0;
}
