/* lambda_nested_capture.cc — 嵌套 lambda 再捕获外层已捕获的变量
 * （缺陷 T 回归，m++ end-to-end）。
 *
 * 缺陷 T（2026-08-03 修复）：外层 lambda 捕获的变量降级为闭包类成员
 * 而非局部变量，内层 lambda 的捕获查找只在真实外围函数局部作用域里
 * 找名字，于是 `[base]` 里出现 error: cannot capture variable 'base'。
 * 修复后捕获查找在 scopegetdecl 失败时回落到当前方法类成员解析
 * （cpp_member_ident），把 `(*this).base` 视作可捕获实体。
 *
 * 对照：内层不捕获、直接用外层捕获值参与运算正常（见 lambda.cc）；
 * 单层捕获正常。
 *
 * 每个检查返回不同退出码；退出 0 = 全部通过。
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

    /* 外层捕获 + 内层再捕获 + 外层局部变量混捕 */
    int m = 5;
    auto mix = [m] {
        int loc = 2;
        auto inner = [m, loc] { return m * loc; };
        return inner() + 1;
    };
    if (mix() != 11) return 3;

    return 0;
}
