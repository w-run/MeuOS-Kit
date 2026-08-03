// C++20 requires 表达式：四类需求 + 否定 + 嵌套 + 多参数
template <typename T>
concept HasPlus = requires(T a) { a + a; };

template <typename T>
concept HasValue = requires(T t) { typename T::value_type; };

template <typename T>
concept Addable = requires(T a, T b) { { a + b } -> HasPlus; };

template <typename T>
concept NestedOk = requires(T t) { requires HasPlus<T>; };

struct WithValue { typedef int value_type; };
struct WithoutValue {};

// 简单需求（含概念定义内的使用）
static_assert(requires(int a) { requires HasPlus<int>; });
static_assert(!requires(int a) { requires HasPlus<void>; });
static_assert(requires(int a) { requires HasValue<WithValue>; });
static_assert(!requires(int a) { requires HasValue<WithoutValue>; });
static_assert(requires(int a) { requires NestedOk<int>; });
static_assert(!requires(int a) { requires NestedOk<void>; });

// 类型需求
static_assert(requires(WithValue w) { typename WithValue::value_type; });
static_assert(!requires(WithoutValue w) { typename WithoutValue::value_type; });
static_assert(requires(int) { typename int; });

// 复合需求
static_assert(requires(int a) { { a + a } -> HasPlus; });
static_assert(requires(int a, int b) { { a + b } -> Addable; });
static_assert(requires(int a) { { a + a }; });
static_assert(requires(int a) { { a + a } noexcept; });
static_assert(requires(int a) { { a + a } noexcept(true); });
// 复合需求：返回类型约束为普通类型 / decltype
static_assert(requires(int a) { { a + a } -> int; });
static_assert(requires(int a) { { a + a } -> decltype(a + a); });
static_assert(requires(int a) { { a + a } noexcept -> int; });
static_assert(requires(int a) { { a + a } noexcept -> decltype(a + a); });
static_assert(!requires(double a) { { a + a } -> decltype((int)0); });

// 嵌套需求
static_assert(requires(int a) { requires Addable<int>; });
static_assert(!requires(int a) { requires HasValue<void>; });

// 独立 requires 表达式
static_assert(requires { 1 + 1; });
static_assert(requires(int a) { a + a; });
static_assert(!requires(void a) { a + a; });
static_assert(requires(int a) { a + a; a * a; });

int main() { return 0; }
