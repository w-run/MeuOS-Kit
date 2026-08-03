/* DEFECT (m++ 漏检): 引用未初始化（声明引用却未绑定对象）。
 *
 *   int &r;            // ill-formed: a reference must be initialized
 *
 * m++ 现状: 编译通过（exit 0），未报错。
 * 正常对照: nonconst_ref_temp.neg.cc（引用绑定到临时）m++ 已正确拒绝，
 *           但"完全未初始化"的引用仍被放过。
 * 期望行为: 报 "reference must be initialized" 并拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/uninit_ref.neg.cc
 */
int main(void) {
    int &r;
    return 0;
}
