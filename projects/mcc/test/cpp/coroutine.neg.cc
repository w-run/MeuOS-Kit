/* coroutine.neg.cc — C++20 协程负向测试（m++）。
 *
 * Phase 1 supports co_return (lowered as a direct function return).
 * co_yield and co_await remain unsupported and must be rejected with
 * a clear "not yet supported" diagnostic.
 *
 * 期望：编译失败。
 * 复现：./m++ --specs=host -o /tmp/x test/cpp/coroutine.neg.cc
 */
int gen() {
    co_yield 1;     /* must be rejected: coroutines unsupported */
    return 0;
}

int h() {
    co_await gen(); /* must be rejected */
    return 0;
}
