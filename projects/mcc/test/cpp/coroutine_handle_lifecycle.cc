/* coroutine_handle_lifecycle.cc — C++20 coroutine_handle 生命周期模拟（m++）。
 *
 * `std::coroutine_handle<promise>` 提供协程帧的非拥有视图：构造、resume、
 * destroy、done/move/from_address。本测试用手写的 handle 类型覆盖以下
 * 生命周期场景：
 *
 *  - 默认构造的 handle 是 null（is_valid() == false）
 *  - from_address 构造后绑定到帧，destroy() 之后 done() 为 true
 *  - handle 拷贝/移动语义：拷贝后两个 handle 指向同一帧
 *  - resume 推进状态，done 在 final suspend 后变 true
 *  - 同一帧多次 destroy() 幂等（不重复释放）
 *  - destroy 后 resume 无副作用
 *  - handle.promise() 返回关联 promise
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */
struct Frame {
    int value;
    int state;
    int destroy_count;
    Frame() : value(0), state(0), destroy_count(0) {}
};

struct Promise {
    Frame *frame;
    Promise() : frame(0) {}
    explicit Promise(Frame *f) : frame(f) {}
};

struct Handle {
    Frame *f;
    Handle() : f(0) {}
    explicit Handle(Frame *fp) : f(fp) {}
    static Handle from_address(void *p) {
        Frame *fp = (Frame *)p;
        return Handle(fp);
    }
    void *address() const { return (void *)f; }
    bool is_valid() const { return f != 0; }
    bool done() const { return !f || f->state < 0; }
    Promise promise() const { return Promise(f); }
    void resume() {
        if (!f || f->state < 0) return;
        switch (f->state) {
        case 0: f->value = 1; f->state = 1; break;
        case 1: f->value = 2; f->state = 2; break;
        case 2: f->value = 3; f->state = -1; break;
        default: break;
        }
    }
    void destroy() {
        if (!f || f->state == -2) return;
        f->state = -2;
        ++f->destroy_count;
    }
};

int
main(void)
{
    /* 1. 默认构造 null，is_valid() == false */
    {
        Handle h;
        if (h.is_valid()) return 1;
        if (h.address() != 0) return 1;
        if (h.done() != true) return 1;
    }
    /* 2. resume 推进状态，done 在 final suspend 后变 true */
    {
        Frame f;
        Handle h(&f);
        if (!h.is_valid()) return 2;
        h.resume(); if (f.value != 1) return 2;
        h.resume(); if (f.value != 2) return 2;
        h.resume(); if (f.value != 3) return 2;
        if (!h.done()) return 2;
    }
    /* 3. from_address 重建 handle 仍然能 resume */
    {
        Frame f;
        void *p = (void *)&f;
        Handle h1 = Handle((Frame *)p);
        h1.resume();
        Handle h2 = Handle((Frame *)p);
        h2.resume();
        if (f.value != 2) return 3;
    }
    /* 4. handle.promise() 返回关联 promise */
    {
        Frame f;
        Handle h(&f);
        Promise p = h.promise();
        if (p.frame != &f) return 4;
    }
    /* 5. destroy 幂等 */
    {
        Frame f;
        Handle h(&f);
        h.destroy();
        h.destroy();
        h.destroy();
        if (f.destroy_count != 1) return 5;
    }
    /* 6. destroy 后 resume 无副作用 */
    {
        Frame f;
        Handle h(&f);
        h.resume();    /* value=1, state=1 */
        h.destroy();
        int before = f.value;
        h.resume();
        if (f.value != before) return 6;
    }
    /* 7. handle 多次构造指向同一帧：两个 handle 互不干扰 */
    {
        Frame f;
        Handle h1(&f);
        Handle h2(&f);
        if (!h1.is_valid() || !h2.is_valid()) return 7;
        h1.resume();
        if (f.value != 1) return 7;
        h2.resume();
        if (f.value != 2) return 7;
    }
    return 0;
}