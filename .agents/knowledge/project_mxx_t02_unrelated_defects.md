---
name: mxx T02/D1 之外发现的预存在缺陷
description: mxx m++ 类模板/成员函数解析的独立缺陷（非 D1 运算符级联），2026-08-03 探测确认
type: project
---

2026-08-03 在实现 mcc-team-r7 的 T02（D1 运算符 const T& 决议，alice 608f31c 已初修，见 c798d1d 补模板边界）时，探测出几个**非本任务范围**的预存在缺陷，均已在主线上用不含 operator 的最小用例复现，属 m++ 类模板/成员解析独立缺陷：

1. **普通成员函数 + 类类型引用形参无法决议**：`struct A { bool same(const A&) const; }` → `a.same(b)` 报 `no matching member function`。非模板、无 operator 即可复现（p10）。这是 D1 运算符路径之外的普通成员调用重载缺陷。
2. **模板成员函数/运算符的 `T&&` 形参解析崩溃**：类模板内任一成员用 `P&&` 形参（含 `operator<(const P<T>&&)`）→ `期望 ')' after parenthesized declarator`，破坏整个类体解析（p21/p22/p23）。非 operator 的普通成员 `void m(P&&)` 同样失败。
3. **类模板第二个实例化的构造函数无法决议**：`Box<int>` 正常，但同文件内 `Box<long>` 的 ctor 报 `没有匹配的构造函数`（p12）。只首个实例化能决议 ctor。
4. **自由函数模板 + 类模板实例 const 引用形参推导失败**：`template<class T> bool f(const Box<T>&)` 内访问 `x.a` 报 `'==' 操作数无效` / `指针赋值类型不兼容`（p15/p16）。

**Why:** T02 任务要求测试模板边界，这些缺陷阻断了 `const Box<T>&` / `Box<T>&&` 在模板内的完整覆盖，但均不属于 D1 的 cpp_try_operator_call 级联查找（无 operator 也可复现），故按纪律只报告不修改。

**How to apply:** 后续若排 mxx m++ 类模板/成员调用任务，优先查这 4 项；测试尽量避开 `Box<char>/Box<long>` 第二实例化 ctor 与模板内 `P&&` 形参，或用单实例化 + 值形参规避。相关探针在 /tmp/d1probe/p10/p21/p12/p16.cc。
