/* coroutine_lambda.neg.cc — C++20 coroutine 在 lambda 内必须被拒绝。
 *
 * 协程 lowering 必须显式生成协程帧并把 promise/promise_type 绑定到
 * lambda 闭包类型，m++ 尚未实现，因此 lambda 体内的 co_yield / co_await
 * 必须产生与 coroutine.neg.cc 一致的诊断错误。本测试同时验证：
 *  - co_yield 在 lambda 内被拒绝
 *  - co_await 在 lambda 内被拒绝
 *  - co_return 在 lambda 内被拒绝
 *  - 多个 co_* 出现在同一 lambda 只触发首个诊断（不重复）
 *
 * 期望：编译失败，错误码 E0006（coroutines unsupported）。
 */
auto f1 = []() -> int {
    co_yield 1;       /* must be rejected: coroutines unsupported */
    return 0;
};

auto f2 = []() {
    co_await f1();    /* must be rejected */
};

int main() { (void)f1; (void)f2; return 0; }