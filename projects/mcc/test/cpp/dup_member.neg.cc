/* DEFECT (m++ 漏检): 类成员重名（同一 struct 内重复成员）。
 *
 *   struct A { int x; int x; };   // ill-formed in C++
 *
 * m++ 现状: 编译通过（exit 0），未报错。
 * 正常对照: redefinition_func.neg.cc / redecl_mismatch.neg.cc 已正确
 *           拒绝函数重定义，说明成员符号表未做类内唯一性检查。
 * 期望行为: 报 "redefinition of 'x'" 并拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/dup_member.neg.cc
 */
struct A { int x; int x; };
int main(void) { A a; return a.x; }
