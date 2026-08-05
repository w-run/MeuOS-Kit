// 定位：无参数简单需求 vs 有参数
static_assert(requires { 1 + 1; }, "no-param simple");
static_assert(requires(int a) { a; }, "param ref only");
static_assert(requires(int a) { a + a; }, "param simple");
int main(){ return 0; }
