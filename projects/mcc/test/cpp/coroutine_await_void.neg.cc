/* coroutine_await_void.neg.cc — co_await 对 void 操作数必须被拒绝。
 *
 * co_await 要求操作数是 awaitable（具备 operator co_await / await_ready /
 * await_suspend / await_resolve）。void 没有这些成员函数。m++ 在协程
 * 语义尚未实现的情况下统一拒绝 co_*，因此无论 awaiter 类型如何都必须
 * 被诊断。
 *
 * 期望：编译失败，错误码 E0006。
 */
int main() {
    co_await;         /* must be rejected: missing await expression */
    return 0;
}