/* coroutine.neg.cc — C++20 协程负向测试（m++）。
 *
 * co_return / co_yield / co_await 的语句语法已被解析器识别（关键字
 * 在 C++ 词法表 CPP_TCO_AWAIT/CPP_TCO_YIELD/CPP_TCO_RETURN 中），
 * 但协程 lowering（promise 协议、协程帧分配、挂起点状态机变换）尚未
 * 实现，因此任何协程语句都必须被拒绝，并给出明确的
 * "C++20 coroutines are not yet supported by m++ (co_*)" 诊断
 * （而非通用的 "undeclared identifier: co_*"）。
 *
 * 期望：编译失败。
 * 复现：./m++ --specs=host -o /tmp/x test/cpp/coroutine.neg.cc
 */
int gen() {
    co_yield 1;     /* must be rejected: coroutines unsupported */
    return 0;
}

int f() {
    co_return 42;   /* must be rejected */
}

void g() {
    co_return;      /* must be rejected (implicit return_void form) */
}

int h() {
    co_await f();   /* must be rejected */
    return 0;
}
