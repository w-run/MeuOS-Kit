/* DEFECT (m++ 漏检): redefinition of a namespace-scope variable.
 *
 *   int a;
 *   int a;        // redefinition of 'a' — ill-formed in C++
 *
 * m++ 现状: 编译通过（exit 0），未报错。
 * 正常对照: block_dup_var.neg.cc（块内重定义）m++ 已正确拒绝，说明
 *           m++ 的重复定义检查未覆盖命名空间作用域的变量重定义。
 * 期望行为: 报 "redefinition of 'a'" 并拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/redef_global_var.neg.cc
 *       (期望失败，但当前成功)
 */
int a;
int a;
int main(void) { return a; }
