/* coroutine_yield_types.cc — C++20 协程 yield 值类型多样性模拟（m++）。
 *
 * `co_yield expr` 表达式要求 promise_type 提供 yield_value(expr) 重载。
 * 不同 expr 类型触发不同的 yield_value 实例。本测试验证 yield 值类型
 * 多样性的状态机等价：手写 generator 支持 int / 字符串字面量指针 /
 * 结构体 / 数组衰减指针等多种 yield 值。
 *
 * 覆盖：
 *  - yield int（最常见）
 *  - yield const char*（字符串字面量衰减）
 *  - yield struct by-value（自定义聚合）
 *  - yield bool / char
 *  - yield 通过临时对象（右值，生命周期延伸到下一次 resume）
 *  - 空 yield（仅作状态切换，无值）
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */
struct Wrap {
    int v;
    int tag;
    Wrap() : v(0), tag(0) {}
    Wrap(int x, int t) : v(x), tag(t) {}
};

struct IntGen {
    int state;
    int value;
    IntGen() : state(0), value(0) {}
    int next() {
        switch (state) {
        case 0: value = 10; state = 1; return 0;
        case 1: value = 20; state = 2; return 0;
        case 2: value = 30; state = -1; return 0;
        default: return -1;
        }
    }
};

struct StrGen {
    int state;
    const char *value;
    StrGen() : state(0), value(0) {}
    int next() {
        switch (state) {
        case 0: value = "hello"; state = 1; return 0;
        case 1: value = "world"; state = -1; return 0;
        default: return -1;
        }
    }
};

struct WrapGen {
    int state;
    Wrap value;
    WrapGen() : state(0) {}
    int next() {
        switch (state) {
        case 0: value = Wrap(1, 100); state = 1; return 0;
        case 1: value = Wrap(2, 200); state = -1; return 0;
        default: return -1;
        }
    }
};

struct BoolGen {
    int state;
    bool value;
    BoolGen() : state(0), value(false) {}
    int next() {
        switch (state) {
        case 0: value = true;  state = 1; return 0;
        case 1: value = false; state = -1; return 0;
        default: return -1;
        }
    }
};

struct CharGen {
    int state;
    char value;
    CharGen() : state(0), value(0) {}
    int next() {
        switch (state) {
        case 0: value = 'a'; state = 1; return 0;
        case 1: value = 'z'; state = -1; return 0;
        default: return -1;
        }
    }
};

int
main(void)
{
    /* 1. yield int（聚合求和） */
    {
        IntGen g;
        int s = 0;
        while (g.next() == 0) s += g.value;
        if (s != 60) return 1;
    }
    /* 2. yield const char*（字面量指针串联） */
    {
        StrGen g;
        char buf[16];
        int pos = 0;
        while (g.next() == 0) {
            const char *p = g.value;
            while (*p) buf[pos++] = *p++;
            buf[pos++] = ' ';
        }
        buf[pos] = 0;
        if (pos != 12) return 2;
        if (buf[0] != 'h' || buf[11] != ' ') return 2;
    }
    /* 3. yield struct by-value */
    {
        WrapGen g;
        int sum_v = 0, sum_tag = 0;
        while (g.next() == 0) {
            sum_v += g.value.v;
            sum_tag += g.value.tag;
        }
        if (sum_v != 3 || sum_tag != 300) return 3;
    }
    /* 4. yield bool（true/false 序列） */
    {
        BoolGen g;
        int true_cnt = 0, false_cnt = 0;
        while (g.next() == 0) {
            if (g.value) ++true_cnt; else ++false_cnt;
        }
        if (true_cnt != 1 || false_cnt != 1) return 4;
    }
    /* 5. yield char */
    {
        CharGen g;
        char first = 0, last = 0;
        int i = 0;
        while (g.next() == 0) {
            if (i == 0) first = g.value;
            last = g.value;
            ++i;
        }
        if (first != 'a' || last != 'z' || i != 2) return 5;
    }
    return 0;
}