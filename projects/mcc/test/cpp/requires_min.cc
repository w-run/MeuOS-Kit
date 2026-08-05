// 最小复现：独立 requires 表达式应编译为布尔常量
static_assert(requires(int a){ a+a; });
int main(){ return 0; }
