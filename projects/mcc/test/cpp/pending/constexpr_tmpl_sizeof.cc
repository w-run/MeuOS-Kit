/* constexpr_tmpl_sizeof.cc — 已知缺陷复现（cpp-10，worker-cpp23 登记）.
 *
 * 缺陷：constexpr 模板函数体在常量求值时，形如 `sizeof(T)` 的
 * 模板参数类型用法报「undeclared identifier: T」。
 *
 * 当前行为：
 *   template <typename T> constexpr int h(T x) { return sizeof(T); }
 *   constexpr int a = h(5);   // error: undeclared identifier: T
 *
 * 已确认的正常对照路径：
 *   - 模板参数的「值用法」正常：`template <typename T> constexpr T
 *     m2(T x){ return x*2; }` + `constexpr int a = m2(21)` 通过；
 *   - 非 constexpr 模板 + `sizeof(T)`：运行期调用正常（z2 对照）；
 *   - constexpr 模板 + `sizeof(T)` + 运行期实参：正常（z1 对照）——
 *     说明**运行时定义解析**路径的模板实例化会正确重写 `T`，失败
 *     只在**常量求值**路径。
 *
 * 根因假设：cpp_buffer_constexpr_body（cpp_parse.c）在模板定义时
 * 缓冲 `{...}` 体 token（含 `T`）；模板实例化重写 + 回放的是运行期
 * 定义解析路径，而 cpp_constexpr_eval 的临时 scope 只是 filescope
 * 的普通子作用域 + 形参绑定，不含模板参数 `T` 的类型绑定 →
 * `sizeof(T)` 中 T 无法解析。单 return 体（本文件）亦复现，
 * 与 G1 多语句解释器无关（G1 之前同样失败）。
 *
 * 修复方向：constexpr 体缓冲在模板实例化时重新缓冲/重写（复用
 * 实例化的 token 重写机制），或求值器 scope 继承模板参数绑定。
 *
 * 期望行为：`sizeof(T)` 在常量求值时解析为实例化类型的尺寸。
 */
template <typename T> constexpr int h(T x) { return sizeof(T); }

int
main(void)
{
    constexpr int a = h(5);
    if (a != 4)
        return 1;
    return 0;
}
