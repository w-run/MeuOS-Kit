/* coroutine_yield_void.neg.cc — co_yield 在 void / 模板函数内必须被拒绝。
 *
 * co_yield 不允许省略操作数（不是语句）；void 返回函数不能是协程；模板
 * 协程在没有 promise_type 实例化的情况下不允许 co_yield。
 *
 * 期望：编译失败。
 */
template <typename T>
T gen() {
    co_yield T();     /* must be rejected */
    return T();
}

void vgen() {
    co_yield 1;       /* must be rejected */
}

int gen2() {
    co_yield;         /* must be rejected: missing expression */
    return 0;
}

int main() { (void)gen<int>; (void)vgen; (void)gen2; return 0; }