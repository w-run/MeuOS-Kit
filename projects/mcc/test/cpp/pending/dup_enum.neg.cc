/* DEFECT (m++ 漏检): 枚举值重名（同一 enum 内重复枚举符）。
 *
 *   enum E { a, a };   // ill-formed in C++
 *
 * m++ 现状: 编译通过（exit 0），未报错。
 * 期望行为: 报 "redefinition of enumerator 'a'" 并拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/dup_enum.neg.cc
 */
enum E { a, a };
int main(void) { return (int)a; }
