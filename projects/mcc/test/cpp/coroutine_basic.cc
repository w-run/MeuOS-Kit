/* coroutine_basic.cc — C++20 协程基础：手写 switch-case 状态机的
 * 简化协程（m++）。
 *
 * m++ 尚未实现协程 lowering（promise 协议 + 协程帧 + 挂起点状态机
 * 变换），co_await / co_yield / co_return 语句会被拒绝并给出明确的
 * "C++20 coroutines are not yet supported" 诊断（见 coroutine.neg.cc）。
 * 本测试验证最简的协程编程模式——手写 switch-case 状态机：每次
 * next() 调用恢复状态、产出值、再挂起，由调用方驱动执行。这正是
 * 编译器生成的协程框架（initial_suspend → 各挂起点 → final_suspend）
 * 所模拟的形态，也证明了 m++ 对成员函数、switch 状态机、聚合初始化
 * 的组合支持。
 *
 * 每个检查返回不同退出码；退出 0 = 全部通过。
 */
struct Generator {
    int state;
    int value;
    int next() {
        switch (state) {
        case 0:
            value = 1;
            state = 1;
            return 0;
        case 1:
            value = 2;
            state = 2;
            return 0;
        default:
            return -1;
        }
    }
};

int
main(void)
{
    Generator g = {0, 0};
    if (g.next() != 0 || g.value != 1) return 1;
    if (g.next() != 0 || g.value != 2) return 2;
    return 0;
}
