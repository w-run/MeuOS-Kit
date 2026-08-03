// C++20 consteval 即时调用强制负向测试：consteval 函数以非常量实参
// 调用必须编译失败（immediate invocation 必须为常量表达式）。
//
// 期望：编译失败，stderr 含 "call to consteval function 'sq' is not a
// constant expression"。
// 复现：./m++ --specs=host -o /tmp/x test/cpp/consteval_nonconst.neg.cc
consteval int sq(int n) { return n * n; }

int main() {
    int v = 7;
    int r = sq(v);   /* 非常量实参：必须报错 */
    return r == 49 ? 0 : 1;
}
