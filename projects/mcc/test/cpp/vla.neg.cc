/* DEFECT (m++ 漏检): C++ 不支持变长数组（VLA）。
 *
 *   int n = 5; int arr[n];   // ill-formed in C++ (VLA is a C feature)
 *
 * m++ 现状: 编译通过（exit 0）—— 作为 C 编译器接受了 VLA，但 m++
 *           (C++ 模式) 应拒绝。
 * 期望行为: 报 "variable-length array not allowed in C++" 或等价错误并
 *           拒绝编译。
 * 复现: ./m++ --specs=host -o /tmp/x test/cpp/pending/vla.neg.cc
 */
int main(void) {
    int n = 5;
    int arr[n];
    return arr[0];
}
