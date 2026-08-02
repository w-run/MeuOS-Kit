/* 限制项记录：文件作用域自由函数重载不支持
 *
 * 当前行为（worktree-mxx-work，2026-08-02 测试矩阵扩充中发现）：
 *   文件作用域声明同名自由函数（无论参数个数还是参数类型不同）均报错：
 *     error: function 'helper' redeclared with incompatible type
 *   复现：
 *     int helper(int a) { return a + 1; }
 *     int helper(int a, int b) { return a + b; }   // 参数个数不同
 *     int helper(double a) { ... }                  // 参数类型不同
 *   均被拒绝。
 *
 * 已确认正常路径（避免误判为回归）：
 *   - 类成员函数重载 正常（ref_ctor.cc 的 f(Vec)/f(Vec&)、
 *     ambig_addr.neg.cc 的 add(int)/add(int,int)、multi_ambig 等）
 *   - 单一自由函数定义 正常（free_operator.cc 的 operator+）
 *   - 运算符重载的按参数编码符号（operator_pl 等）正常
 *
 * 根因：疑似文件作用域函数注册直接按名称去重，未对自由函数走
 *       参数编码符号的过载注册路径（成员函数有 Class_meth_ii 编码，
 *       自由函数只注册 name）。需 src/ 实现侧确认。
 *
 * 期望（若重载范围含自由函数）：两个 helper 共存，按参数解析。
 */
int helper(int a) { return a + 1; }
int helper(int a, int b) { return a + b; }

int main(void) {
    if (helper(5) != 6) return 1;
    if (helper(5, 3) != 8) return 2;
    return 0;
}
