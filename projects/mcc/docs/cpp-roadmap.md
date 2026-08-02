# m++ C++ 前端路线图（cpp-roadmap.md）

> 状态：规划文档（planner 产出，2026-08-02；成员模板已完成，见 c93d5f7）。
> 用途：为 m++ 未实现特性（C++11 lambda → constexpr → 变参模板 → 移动语义 → C++14/17/20）提供实现顺序、参考源与降级策略的决策依据。
> 约束：不写代码；报告事实均来自实际文件/搜索，标注参考源路径。

## 0. 调研结论摘要

- **m++ 现状**（实测）：`src/cpp/` 仅两文件（`lex/cpp_scan.c` 139 行 + `parse/cpp_parse.c` 3301 行），走「词法 → 语法/语义合一 → 复用 C 前端 decl/expr → 共享 MIR 后端」路线。已实现 C.2.3 构造/析构、C.2.5 虚函数/vtable、C.2.8 函数/类模板（instantiate-on-first-use，token 缓冲回放式）、成员模板（C.2.8，c93d5f7 完成：类内 `template` 方法注册 + `obj.get<int>(...)` 调用点实例化，`cpp.h` 提供 `cpp_tmpl_lookup/placeholder/instantiate/class_*`）。
- **模板机制特点**：非 AST 模板，而是**token 缓冲回放**（`cpp_template_decl` 存 `toks[]`，实例化时把参数绑定为 DECLTYPE、重命名函数 token、`tokpush` 回放重解析）。这是极简路径，但参数推导只支持位置式（`cpp_tmpl_deduce`），无 SFINAE/偏特化/两阶段查找。
- **参考源可用性**：
  - `reference/aburiscript/`（19 万行 C++ 前端→LLVM）：**唯一完整可参考的降级实现**，含 `constexpr/consteval_engine.cpp`（AST 解释器）、`collect/collect_templates_packs.cpp`（包展开）、`collect/collect_overload.cpp`（重载决议）、`ast2llvm/lower_*.cpp`（降级 lowering）、`abi/mangle.cpp`。模块可按需摘取思路，但代码是完整 C++ AST 架构，m++ 是 token 回放架构，**不能直接搬代码，只能借鉴算法**。
  - `reference/cplusplus/`（C++26 解析库，94K 行 80 文件）：`overload_resolution.cc`（1065 行）、`template_argument_deduction.cc`（729 行）、`ast_rewriter_instantiate.cc`（913 行）、`ast_rewriter_packs.cc`、`ast_rewriter_requires.cc`。**只做解析/绑定，无 lowering**，且是 CMake+flatbuffers 架构。可参考的仅是「重载/推导算法的边界条件清单」，实现要自研。
  - `reference/clank/`：空，不可用（README 已注明）。
- **社区调研**（WebSearch，2026-08-02）：小型自研 C++ 编译器公开项目极少——chibicc/tinycc 均为纯 C 前端，无 C++ 扩展；Arkari 是 LLVM 混淆器非自研前端；SCPP 有 constexpr 引擎设计文档（`scpp-lang.org/design/zh/constexpr-engine-design.html`，AST 解释器思路）。**结论：无现成小型 C++ 前端可整包借鉴，m++ 必须走"自研核心 + aburi 算法参考"路线**。

---

## 1. 推荐实现顺序（按依赖 + 收益排序）

| 序 | 特性 | 前置依赖 | 收益 | 难度 |
|---|---|---|---|---|
| 0 | 成员模板 ✅（已完成，c93d5f7） | C.2.8 模板 | 中（类模板配套） | 中 |
| 1 | **auto / decltype** | 无（token 已留 CPP_TAUTO/TDECLTYPE） | 高（一切现代代码的前置） | 低 |
| 2 | **变参模板 / 包展开** | C.2.8 模板 token 回放 | 高（STL/现代代码基础） | 中高 |
| 3 | **lambda（匿名类降级）** | 类/构造/捕获；auto（可后补） | 高 | 中 |
| 4 | **constexpr 求值器** | 常量折叠/表达式树；变参模板可选 | 高（编译期计算） | 高 |
| 5 | **移动语义（右值引用）** | 重载决议 + 构造/析构 | 中高（性能） | 高 |
| 6 | C++14/17（泛型 lambda/if constexpr/CTAD/结构化绑定） | 上面全部 | 渐进 | 递进 |
| 7 | concepts/requires | 模板 + 重载完备 | 中 | 高 |

> 建议的**迭代骨架**：「auto/decltype → 变参模板 → lambda → constexpr → 移动语义」（成员模板已随 c93d5f7 完成，从骨架移除）。此顺序的理由见 §4 对任务给定路线的具体分析。

---

## 2. 每项特性的实现路径

### 2.1 auto / decltype（推荐最先）
- **机制**：`auto` 作为占位类型，在声明初始化处（`auto x = expr;`）或返回类型（`auto f() -> T`，后者 C++14 才推广）用表达式类型回填。m++ 已有完整类型系统 + 表达式树，`type` 指针现成，只需在 decl 解析处延迟定类型。
- **参考**：
  - `reference/aburiscript/helpers/auto_type_utils.cpp`（auto 类型推导工具）。
  - `reference/aburiscript/collect/collect_decl_variable.cpp`（变量声明的类型回填）。
- **降级策略**：先只支持 `auto x = <expr>`（无引用折叠、无 `auto&`），返回类型 auto 放 C++14 段。**无需完整实现即可解锁大量现代语法**。

### 2.2 变参模板 / 包展开
- **机制**：m++ 的 token 回放模板天然适合"展开式"处理——`template <typename... Ts>` 的包参数在实例化时复制 token 序列 N 次并绑定。关键难点是包参数在参数列表/函数体中的**定位与重复**。
- **参考**：
  - `reference/aburiscript/collect/collect_templates_packs.cpp`（1271 行，包参数解析/展开/双查找策略）。
  - `reference/aburiscript/collect/collect_templates_arguments.cpp`（模板实参收集）。
  - `reference/cplusplus/src/parser/cxx/ast_rewriter_packs.cc`（包展开重写）。
- **降级策略**：先只支持**函数参数包 + 简单展开**（`Ts... args` + `f(args...)`），不支持折叠表达式（C++17）、包于包嵌套、非类型包。测试用 `max_n(1,2,3,4)` / 递归模板求值。
- **风险**：token 回放对"包元素逐个实例化"（如 `sizeof...(Ts)`、递归 `sum<Ts...>`）需要每元素一次完整回放，编译期开销随包长线性增长——**先用小包 + 深递归限制**控制。

### 2.3 lambda（匿名类降级）
- **机制**（社区一致结论 + aburi 佐证）：lambda 本质是编译器合成的匿名类（closure type），成员 = 捕获变量，`operator()` = lambda 体。m++ 已有类/成员函数/运算符重载/构造函数链，**完全可复用**。
- **参考**：
  - `reference/aburiscript/parser/parser_expr.cpp:1771` `parse_cpp_lambda_expression`（捕获列表解析：值/引用/this/init-capture 的完整枚举，是捕获语法的边界条件清单）。
  - `reference/aburiscript/collect/collect_expr.cpp:1264` `build_lambda_semantic_captures` + `collect_finalize_cpp_lambda_expression`（闭包信息→语义信息，含无捕获 lambda 生成 `__invoke` 函数指针的优化——**无捕获 lambda 可降级为普通 static 函数指针**）。
  - `reference/aburiscript/ast2llvm/ast2llvm.cpp:1257` lambda invoker lowering。
- **降级策略（重要简化）**：
  1. **无捕获 lambda** → 直接生成一个 static 函数 + 指向它的函数指针，零闭包结构。
  2. **按值捕获** → 匿名类成员 + 合成构造函数。
  3. **按引用捕获** → 成员为指针/引用。
  4. 泛型 lambda（`auto` 参数）放 C++14 段。
- **风险**：闭包类型需要唯一命名（mangle 规则）、捕获变量作用域在成员方法体内解析（`cpp_member_ident` 已有裸成员名解析，可复用）。m++ 无局部类（类定义在函数内）限制——若闭包定义进函数体需评估 `cpp_template_decl`/类定义路径的嵌套能力。

### 2.4 constexpr 求值器
- **机制**：AST/表达式树解释器。m++ 表达式已是 C 前端的 `struct expr`（常量折叠 `mkconstexpr` 大量使用），**求值器可直接解释 expr 树**，无需新建 IR。
- **参考**：
  - `reference/aburiscript/constexpr/consteval_engine.cpp`（4728 行，完整 AST 解释器：`InterpreterSession`/栈帧/`EvalMemory` 内存模型/const 对象表）。
  - `reference/aburiscript/constexpr/consteval_result.h` / `const_value.h` / `eval_memory.h`（求值结果 + 内存模型的数据结构设计）。
  - SCPP constexpr 引擎设计文档（AST 解释器思路，社区参考）。
- **降级策略（关键）**：
  - **阶段 1（推荐）**：仅常量折叠扩展——`constexpr int` 变量 + `constexpr` 函数（纯算术/比较/数组索引），用现有 fold 能力解释。
  - **阶段 2**：`static_assert` + 数组维度 + 模板实参（非类型）的编译期求值。
  - **阶段 3**：constexpr 对象（含成员访问）→ 需要 mini 内存模型（aburi `EvalMemory` 思路）。
- **风险**：求值器与 C 后端折叠逻辑重叠，需明确"谁负责"（建议求值器只读 expr 树、输出 MConst，不依赖 IR）。`constexpr` 递归深度、循环（C++14 放宽）需限制防止编译期挂起。

### 2.5 移动语义（右值引用）
- **机制**：`T&&` 类型 + 引用折叠 + 重载决议对「值类别（lvalue/rvalue）」的选择 + 移动构造/移动赋值。m++ 已有引用参数（`prefer_ref` mangle 'R' 标记）与重载决议雏形。
- **参考**：
  - `reference/aburiscript/collect/collect_overload.cpp`（重载候选排序，含引用限定符；1033-1340 行附近有参数绑定/候选生成）。
  - `reference/aburiscript/collect/collect_init.cpp`（初始化序列选择复制/移动构造）。
  - `reference/aburiscript/ast/types.cpp`（右值引用类型表示）。
  - `reference/cplusplus/src/parser/cxx/overload_resolution.cc`（值类别/引用折叠的边界条件）。
- **降级策略**：**必须完整做引用折叠与值类别判断**（这是 C++11 核心语义，无法跳过），但可以先不实现 `std::move` 内建特化（库层实现）、不做 `&&` 模板参数（转发引用）的完美转发——后者用重载替代。
- **风险**：值类别判定（`return local;` 触发移动、`x + y` 临时量、`std::move(x)` 显式转换）需要给 expr 加 rvalue/lvalue 标记，**触及 C 前端 expr 结构**，改动面大，建议最后做。

### 2.6 C++14/17（后续）
- 泛型 lambda / auto 返回：建立在 2.1 + 2.3 之上，主要难度在「auto 参数模板化」= lambda 的 operator() 变成函数模板（token 回放可做）。
- if constexpr / fold expressions：基于 2.2 包展开 + 2.4 求值器。
- CTAD / 结构化绑定：class template argument deduction 基于 2.2 推导；结构化绑定是解构语法糖。
- concepts/requires：`reference/cplusplus/src/parser/cxx/ast_rewriter_requires.cc` 可参考约束子句解析；但约束求值依赖 2.4 constexpr——**放最后**。

---

## 3. 风险与简化策略汇总

| 特性 | 可降级 | 必须完整 | 关键风险 |
|---|---|---|---|
| auto/decltype | 只做 `auto x = expr` | 类型回填正确性 | 无 |
| 变参模板 | 只做函数包+简单展开 | 包定位/重复规则 | 回放开销线性增长 |
| lambda | 无捕获→static fn 指针；值捕获→匿名类 | 捕获语义正确 | 局部类定义嵌套能力 |
| constexpr | 阶段 1 纯算术折叠即可交付 | 求值器正确性（不进 IR 无限循环） | 与 C fold 职责重叠 |
| 移动语义 | 可不做完美转发 | **引用折叠 + 值类别** | 触达 expr 结构，改动面大 |

**全局原则**：m++ 的 token 回放模板是「算法参考 aburi、机制自研」的放大器——aburi 是完整 AST 架构，其 collect/constexpr 模块提供**语义规则清单**，但每个特性的落地都要用 m++ 自己的 token+decl/expr 机制重写，**不要试图移植代码**。

---

## 4. 对给定路线「成员模板 → C++11 lambda → constexpr → 变参模板 → 移动语义」的评估与建议

任务给定的顺序与计划 C.3（C.3.1 移动语义在前）不同。结合依赖分析给出修正建议：

- **成员模板** ✅ 已按建议最先完成（c93d5f7，依赖 C.2.8 既有模板机制，改动最小）。
- **lambda 提前到第 2-3 位** ⚠️ 偏早：lambda 语法本身不依赖 constexpr/变参模板，**前置只需 auto 或类机制**；但 lambda 捕获/闭包语义依赖引用与构造（已有）。**建议：在 lambda 之前先插一个轻量 auto/decltype 步骤**（§2.1），否则 `auto f = [&]{};` 无法声明，lambda 价值大打折扣。
- **constexpr 放在 lambda 之后** ✅ 合理：constexpr 求值器是独立大模块（难度最高），放在 lambda（难度中、回报直观）之后可先积累表达式/类型处理经验；且 C++11 constexpr 实际常配合变参模板做编译期计算——**变参模板宜提到 constexpr 之前**。
- **变参模板排在 constexpr 之后** ⚠️ 建议前移：变参模板是 STL/现代 C++ 的地基，且 constexpr 的典型用例（编译期求和/递归类型）需要包展开支撑；token 回放机制下包展开属于"模板系统的自然延伸"，先做可让 constexpr 阶段的模板实参传递更顺。
- **移动语义最后** ✅ 合理：依赖重载决议成熟 + expr 值类别改造（改动面大），且无移动语义 C++98 风格代码仍可编译，收益主要体现在性能与 STL——放最后符合"先求正确、后求现代"。

### 修正后的推荐路线
```
✅成员模板(已完成 c93d5f7) → auto/decltype → 变参模板/包展开 → lambda → constexpr 求值器 → 移动语义 → C++14/17 → concepts
```
每步验收（对接现有 `test/cpp/` + Makefile `check-cpp-*`）：
1. auto：`auto x = 42; auto s = f();` 编译运行。
2. 变参：`template<typename... Ts> int sum(Ts... a)` 递归求和。
3. lambda：`[&](){}` / `[=]` / 无捕获转函数指针三档测试。
4. constexpr：`constexpr int sq(int n){return n*n;}` + static_assert。
5. 移动：`Vec(Vec&&)` 触发 + 值类别断言。

---

## 5. 参考源文件索引

| 特性 | 参考源（实际存在） |
|---|---|
| auto | `reference/aburiscript/helpers/auto_type_utils.cpp`、`collect/collect_decl_variable.cpp` |
| 变参模板 | `reference/aburiscript/collect/collect_templates_packs.cpp`、`collect_templates_arguments.cpp`、`reference/cplusplus/src/parser/cxx/ast_rewriter_packs.cc` |
| lambda | `reference/aburiscript/parser/parser_expr.cpp:1771`、`collect/collect_expr.cpp:1264,2826`、`ast2llvm/ast2llvm.cpp:1257` |
| constexpr | `reference/aburiscript/constexpr/consteval_engine.cpp`、`consteval_result.h`、`eval_memory.h`、`const_value.h` |
| 移动语义 | `reference/aburiscript/collect/collect_overload.cpp`、`collect_init.cpp`、`ast/types.cpp`、`reference/cplusplus/src/parser/cxx/overload_resolution.cc` |
| concepts | `reference/cplusplus/src/parser/cxx/ast_rewriter_requires.cc` |
| m++ 现状 | `projects/mcc/src/cpp/parse/cpp_parse.c`（3301 行）、`include/cpp.h`、`include/cpp/cpp_tokens.h` |

> 社区无小型 C++ 前端可整包借鉴（chibicc/tinycc 纯 C；Arkari 是混淆器；SCPP 仅设计文档）。参考基线 = aburiscript 算法 + cplusplus 边界条件 + 自研 token 回放机制。
