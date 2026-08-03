---
name: m++ C++ 前端缺陷闭环
description: C++23 缺口、依赖 NTTP/constexpr 返回/consteval 边界、缺陷 K/M/N、T02 相关预存在缺陷、D4 非空类返回
type: project
---

# m++ C++ 前端缺陷闭环

## 1. C++23 四缺口 + nodiscard

- **P0849 `auto(x)`**（f7e313a）：decay + 剥顶层 cv 产生 prvalue
- **P1774 `[[assume(expr)]]`**（b54c8b9）：attr.c 识别，表达式未求值 no-op
- **P1401 if constexpr 窄化转 bool**（16c2ca5）：PROPINT→PROPSCALAR
- **P2360 init 语句 alias + using 别名**（2a4d655）：`using Name=Type;` + if init 深度 0 分号判定
- **nodiscard**（da7a107）：丢弃 nodiscard 返回值发 WARN_RETURN（cc_warn 首个消费者）

**token 回放三坑**：
1. `peek(kind)` 命中时会消费 token（内部 next() 两次命中后不恢复）
2. `tokpush()` 不回退 scanner——必须把缓冲区域后一个 token（当前全局 tok）也压栈
3. P2360 不能先试 `decl(s,f)` 再判定（ctor-call 不回溯安全会 error），先扫描判定有无深度 0 分号

## 2. C++ 剩余缺口三项

- **依赖类型 NTTP**（ef89d22）：模板参数表解析期建 ps 作用域，`template<typename T, T N>` 识别为依赖 NTTP
- **constexpr 函数返回类对象**（3ae5a04）：DECL struct 局部聚合初始化 + RETURN 类返回 + eval TMUL 折叠 &IDENT/&EXPRCALL/EXPRCALL 三种形态
- **consteval 模板边界**（d061167）：实测已完整实现，仅补测试

**关键坑**：cpp_tmpl_explicit_parse 的 trial-probe 缓冲会吞 token（混合实参 f<int,5> 失败），重写为按 is_nttp 直接解析；cpp_tmpl_const_arg 缓冲循环只在 TGREATER 停止需补 TCOMMA 深度 0；成员访问 AST 需识别三种形态。expr enum：EXPRIDENT=0, EXPRCONST=1, ..., EXPRBINARY=9。

**未做边界**：`P{...}` 函数式花括号初始化；文件作用域 constexpr 类对象受 IR 发射器限制。

## 3. 缺陷 K/M/N

- **K**：concept 递归深度上限 16，链长 ≥17 编译失败且报错误导。**已修**（a0fc836，16→256）。
- **M**：带未命名参数的 ctor 使 m++ 编译期段错误（`class B{B(int){};...}; B b(3);`）。**待修**，canary `test/cpp/new_delete_unnamed_param.neg.cc`。
- **N**：`new T[n]` 类元素构造 stride 错（TADD 不缩放指针，`&arr[i]` 按字节偏移）。**已修**（754b437，显式 `i*sizeof(T)`）。

## 4. T02/D1 之外的 m++ 预存在缺陷（未修，报告级）

1. 普通成员函数 + 类类型引用形参无法决议（`a.same(b)` → no matching member）
2. 模板成员 `P&&` 形参解析崩溃（破坏整个类体）
3. 类模板第二个实例化的 ctor 无法决议（只首个可用）
4. 自由函数模板 + 类模板实例 const 引用形参推导失败

均无 operator 可复现（非 D1 运算符路径）。后续排类模板/成员调用任务优先查这 4 项。

## 5. 缺陷 D4 非空类按值返回（已闭环）

根因**不在 irgen/func.c ABI 路径**，而在 `cpp_parse.c:emit_base_ctors_for`——标量成员 init-list 项 `: a(7)` 被 `continue` 丢弃。修复（alice 608f31c，合入 5db6f26）：对命中初始化项的非 struct/union 成员发射 `*(this+offset) = v`。回归测试 `ctor_scalar_initlist.cc`。**派 D4 类任务前先 `git branch --contains 608f31c` 确认是否已在主线**。r7 复查双后端对 8~200B 聚合、嵌套、混合浮点、非平凡拷贝、虚函数全通过。注意：`projects/mcc/-` 是误提交的 .s 垃圾文件（-MP 副作用），勿 `git add -A` 带进去。

## 6. mcc -O0 构建触发预存在 UB

C++ 前端有未初始化内存 UB：CFLAGS=-O0 时编译 deducing_this.cc 触发 `tokenstr: Assertion 'kind < tokstr.len'` 崩溃（exit 134），-O2 正常。**门禁一律用默认 CFLAGS（-O2）**，看到 -O0 下 tokenstr 断言不要归因于最近改动，先查构建优化级别。根因在 specs.c/declspecs 路径，未列入待办。
