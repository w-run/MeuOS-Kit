/* 缺陷记录：嵌套 lambda 无法再捕获外层已捕获的变量（defect T）
 *
 * 触发条件（worktree-mxx-work，2026-08-03 测试矩阵扩充中发现）：
 *   内层 lambda 若要捕获外层 lambda 已捕获的变量，编译报错：
 *     int base = 7;
 *     auto outer = [base] {
 *         auto inner = [base] { return base * 2; };
 *         return inner();
 *     };
 *   → error: cannot capture variable 'base'
 *
 * 对照：
 *   - 内层不捕获、直接用外层捕获值参与运算正常：
 *       auto outer = [base] { auto inner = [] { return 2; };
 *                             return inner() * base; };   // OK
 *   - 单层捕获正常（见 test/cpp/lambda.cc 的嵌套 lambda 用例，内层无捕获）。
 *
 * 影响面：任意两层以上的捕获嵌套写法都写不出来；常见于把外层参数
 *       透传进内层回调。
 *
 * 疑似根因：捕获查找只扫描真实的外围函数局部作用域，没把外层 lambda
 *       的捕获成员（降级后的匿名类成员）视作可捕获实体。
 *
 * 期望修复后：本文件 main 返回 0。
 *
 * 注意：本文件在 pending/ 目录，不接入 check-cpp-* glob（避免 CI 红）。
 *       修复后把断言移入 lambda_capture_boundary.cc 并删除本文件。
 */
int
main(void)
{
    int base = 7;

    /* 内层再捕获外层的捕获变量 */
    auto outer = [base] {
        auto inner = [base] { return base * 2; };
        return inner();
    };
    if (outer() != 14) return 1;

    /* 三层嵌套捕获 */
    int k = 3;
    auto l1 = [k] {
        auto l2 = [k] {
            auto l3 = [k] { return k + 1; };
            return l3() * 2;
        };
        return l2();
    };
    if (l1() != 8) return 2;

    return 0;
}
