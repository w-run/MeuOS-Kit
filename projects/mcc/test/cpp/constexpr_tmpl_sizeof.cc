/* constexpr_tmpl_sizeof.cc — defer cpp-10 修复回归.
 *
 * 复现并固化缺陷 cpp-10：constexpr 模板函数被常量实参调用触发常量求值时，
 * 体内形如 `sizeof(T)` 的模板参数「类型用法」会报「undeclared identifier: T」。
 *
 * 根因（已修复）：cpp_buffer_constexpr_body 在模板实例化时缓冲体 token
 * （`T` 仍是字面标识符），而常量求值器 cpp_constexpr_eval 在临时 scope
 * 中只绑定了函数形参、未绑定模板参数。运行时实例化路径把 `T` 作为
 * DECLTYPE 绑定进实例化 scope，因此运行期调用正常；只有常量求值路径失败。
 * 修复：实例化时为 constexpr 体记录模板参数 → 类型绑定，求值器在临时
 * scope 中将其重新绑定为 DECLTYPE，使 `sizeof(T)` 等类型用法可被解析。
 *
 * 以下用例覆盖：常量实参调用、sizeof(T) 作返回值与形参初值、多模板形参、
 * 多语句 constexpr 体、嵌套 constexpr 模板调用，以及运行期实参调用（对照）。
 */

/* 基础：constexpr 模板函数 + 常量实参 + sizeof(T) 作返回值 */
template <typename T> constexpr int h(T x) { return (int)sizeof(T); }

/* sizeof(T) 作为形参初值（类型用法作为声明的一部分） */
template <typename T> constexpr int sz_as_init(T) {
    int n = (int)sizeof(T);
    return n;
}

/* 多模板形参 + 类型用法 */
template <typename A, typename B>
constexpr int pair_size(A, B) {
    return (int)(sizeof(A) + sizeof(B));
}

/* 多语句 constexpr 体（G1 解释器）内用 sizeof(T) */
template <typename T> constexpr int multi_stmt(T) {
    int total = 0;
    int s = (int)sizeof(T);
    total = s + s;
    if (s > 0)
        total = total - s;
    return total;
}

/* 嵌套 constexpr 模板调用（外层模板体调用内层 constexpr 模板） */
template <typename T> constexpr int inner(T) { return (int)sizeof(T); }
template <typename T> constexpr int outer(T x) { return inner(x); }

int
main(void)
{
    /* 常量上下文求 sizeof(int) == 4 */
    constexpr int a = h(5);
    if (a != 4)
        return 1;

    /* sizeof(long) == 8 通过常量实参 long */
    constexpr int b = h(5L);
    if (b != 8)
        return 2;

    /* sizeof(char) == 1 通过常量实参 char */
    constexpr int b1 = h((char)0);
    if (b1 != 1)
        return 20;

    /* sizeof(T) 作为形参初值 */
    constexpr int c = sz_as_init((short)0);
    if (c != 2)
        return 3;

    /* 多模板形参 */
    constexpr int d = pair_size((char)0, (long long)0);
    if (d != 1 + 8)
        return 4;

    /* 多语句体（含 sizeof(T) 与循环/if） */
    constexpr int e = multi_stmt((short)0);
    if (e != 2)
        return 5;

    /* 嵌套 constexpr 模板调用 + 常量求值 */
    constexpr int f = outer(3);
    if (f != 4)
        return 6;

    /* 运行期实参对照：constexpr 模板 + 运行期实参仍正常 */
    long lv = 7;
    int g = h(lv);
    if (g != 8)
        return 7;

    return 0;
}
