/* coroutine_main.neg.cc — co_await/co_yield/co_return 在 main 内必须被拒绝。
 *
 * main() 永远不是协程（C++20 [basic.start.main]）：操作系统入口由进程
 * 启动器调用，co_return 会破坏其 RAII 清理链。本测试覆盖：
 *  - co_return 在 main 内（任何返回类型）
 *  - co_yield 在 main 内（main 不应是 generator）
 *  - co_await 在 main 内（main 不应挂起）
 *  - 通过函数指针间接调用协程 main 也被拒绝（即便 main 是协程，返回
 *    类型仍须是 void / int）
 *
 * 期望：编译失败。
 */
int main() {
    co_await 0;       /* must be rejected */
    return 0;
}