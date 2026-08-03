/* DEFECT (m++ 漏检): 非 void 函数走到末尾未返回值（控制流到达函数
 * 结尾却无 return）。
 *
 *   int f(void) {}      // ill-formed: no return value
 *
 * m++ 现状: 编译通过（exit 0），未报错。
 * 期望行为: 报 "control reaches end of non-void function" 类错误并
 *           拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/missing_return.neg.cc
 */
int f(void) { }
int main(void) { return f(); }
