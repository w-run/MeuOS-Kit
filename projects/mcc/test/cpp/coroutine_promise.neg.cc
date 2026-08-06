/* coroutine_promise.cc — C++20 coroutine promise_type 模拟（m++）。
 *
 * m++ 不实现真正的协程 lowering，但 promise_type 是协程编程的核心抽象：
 * `std::coroutine_traits<R>::promise_type` 由编译器注入到协程帧中并由
 * 协程体的控制流调用（get_return_object / initial_suspend / yield_value /
 * return_void / final_suspend）。本测试用手写状态机 + 显式 promise 对象
 * 模拟这套协议。
 *
 * 覆盖：
 *  - promise 含初始/最终挂起 sentinel
 *  - yield_value / return_value / return_void 三种协议路径
 *  - promise 与 generator 之间的双向引用（通过指针 back-link）
 *  - promise RAII 独立于 generator 作用域（析构语义）
 *  - 同一个 promise 可以从多个 generator 引用
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */

struct IntResult {
    int v;
    IntResult() : v(0) {}
    IntResult(int x) : v(x) {}
};

/* Promise: 持有值 + 挂起状态 + back-link */
struct IntPromise {
    IntResult value;
    int suspended_at_start;
    int suspended_at_end;
    int *back_link;

    IntPromise()
        : value(), suspended_at_start(1), suspended_at_end(1), back_link(0) {}
};

/* Generator: 模拟协程 handle，持有 Promise 指针 */
struct IntGen {
    IntPromise *p;
    IntGen() : p(0) {}
    explicit IntGen(IntPromise *pp) : p(pp) {
        if (p) p->back_link = (int *)(void *)this;
    }

    /* resume: 模拟 initial_suspend → yield_value → return_value */
    int resume() {
        if (!p) return -1;
        if (p->suspended_at_start) {
            p->value = IntResult(7);
            p->suspended_at_start = 0;
            return 0;
        }
        if (p->suspended_at_end) {
            p->value = IntResult(13);
            p->suspended_at_end = 0;
            return 0;
        }
        return -1;
    }
};

int
main(void)
{
    /* 1. promise 初始/最终挂起 sentinel 可独立切换 */
    {
        IntPromise p;
        IntGen g(&p);
        if (p.suspended_at_start != 1 || p.suspended_at_end != 1) return 1;
        if (g.resume() != 0) return 1;
        if (p.suspended_at_start != 0) return 1;
        if (g.resume() != 0) return 1;
        if (p.suspended_at_end != 0) return 1;
        if (g.resume() != -1) return 1;
    }
    /* 2. yield_value 路径：value 正确更新 */
    {
        IntPromise p;
        IntGen g(&p);
        g.resume();
        if (p.value.v != 7) return 2;
        g.resume();
        if (p.value.v != 13) return 3;
    }
    /* 3. promise back-link 在构造时设置 */
    {
        IntPromise p;
        IntGen g(&p);
        if (p.back_link == 0) return 4;
    }
    /* 4. promise RAII 独立于 generator 作用域 */
    {
        IntPromise *p = new IntPromise;
        {
            IntGen g(p);
            g.resume();
        }
        /* generator out of scope; promise still alive */
        if (p->suspended_at_start != 0) return 5;
        delete p;
    }
    /* 5. 空 generator 的 resume 返回 -1 */
    {
        IntGen g;
        if (g.resume() != -1) return 6;
    }
    /* 6. 同一个 promise 可以被多个 generator 使用 */
    {
        IntPromise p;
        IntGen g1(&p);
        IntGen g2(&p);
        g1.resume();
        if (p.value.v != 7) return 7;
        g2.resume();
        if (p.value.v != 13) return 7;
    }
    return 0;
}