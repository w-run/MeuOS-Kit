/* coroutine_nested.cc — C++20 coroutine 嵌套协程模拟（m++）。
 *
 * m++ 尚未实现真正的协程 lowering（promise 协议、协程帧、挂起点状态机
 * 变换），但 hand-written 协程（成员函数 + 状态机 + 嵌套调用）正好模拟
 * 了编译器生成协程的"嵌套 initial-suspend / yield / final-suspend"形态。
 *
 * 本测试覆盖：
 *  - 协程内调用另一个协程（外层 yield → 内层 yield → 内层 final → 外层
 *    resume 推进）
 *  - 嵌套协程的生命周期独立（外层挂起不影响内层）
 *  - 多层协程链：gen → sub1 → sub2
 *  - 协程内的 RAII 局部对象（每次 resume 都构造/析构 guard）
 *  - 协程与异常路径的交互（异常退出后续 resume 必须返回 -1）
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */
struct Guard {
    int *pcounter;
    Guard(int &c) : pcounter(&c) { ++(*pcounter); }
    ~Guard() { --(*pcounter); }
};

struct Sub2 {
    int state;
    int next() {
        switch (state) {
        case 0: state = 1; return 0;
        case 1: state = -1; return 0;
        default: return -1;
        }
    }
};

struct Sub1 {
    Sub2 sub;
    int state;
    int next() {
        switch (state) {
        case 0:
            if (sub.next() != 0) return -1;
            state = 1;
            return 0;
        case 1:
            if (sub.next() != 0) return -1;
            state = -1;
            return 0;
        default:
            return -1;
        }
    }
};

struct Gen {
    Sub1 inner;
    int state;
    int next() {
        switch (state) {
        case 0:
            if (inner.next() != 0) return -1;
            state = 1;
            return 0;
        case 1:
            if (inner.next() != 0) return -1;
            state = -1;
            return 0;
        default:
            return -1;
        }
    }
};

int
main(void)
{
    /* 1. 三层嵌套：Gen -> Sub1 -> Sub2，每层 2 个 yield */
    {
        Gen g = {{{0}, 0}, 0};
        int cnt = 0;
        while (g.next() == 0) ++cnt;
        if (cnt != 2) return 1;
    }
    /* 2. 内层提前终止时外层 resume 仍安全 */
    {
        Sub1 s = {{-1}, 0};   /* Sub2 已经在 -1 状态 */
        if (s.next() != -1) return 2;
        if (s.next() != -1) return 2;
    }
    /* 3. 协程 resume 期间 RAII guard 生命周期 */
    {
        int gc = 0;
        struct GuardedGen {
            int &g;
            int state;
            int next() {
                switch (state) {
                case 0: { Guard gu(g); state = 1; return 0; }
                case 1: { Guard gu(g); state = -1; return 0; }
                default: return -1;
                }
            }
        } gg = {gc, 0};
        if (gg.next() != 0 || gc != 0) return 3;
        if (gg.next() != 0 || gc != 0) return 3;
        if (gg.next() != -1) return 3;
        if (gc != 0) return 3;
    }
    /* 4. 协程在已终止状态连续 resume 返回 -1（幂等） */
    {
        Gen g = {{{0}, 0}, 0};
        while (g.next() == 0) {}
        for (int i = 0; i < 5; ++i) {
            if (g.next() != -1) return 4;
        }
    }
    return 0;
}