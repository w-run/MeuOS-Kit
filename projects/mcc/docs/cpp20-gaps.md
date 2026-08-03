# m++ C++20 剩余特性缺口路线图（cpp20-gaps.md）

> 状态：调研文档（worker-cpp20 产出，2026-08-03）。只调研+实测，不改业务代码。
> 基线：HEAD 6142ec5（worktree-mxx-work），`m++ 0.1.0`，`--specs=host` 实测。
> 目的：对照 C++20 core language（非标准库）特性清单，逐项核实 m++ 支持情况，输出缺口/证据/难度/优先级。
> **复核（worker-req，2026-08-03）**：HEAD 已推进到 e06a2b0（worktree-mxx-work）。此后落地了 range-for（71fbb35）、
> constexpr 多语句解释器（dca1620）、多维 operator[]（cfeadf9）、if consteval（55499d6）、deducing this（bea068d）、
> operator[] const 决议（da9e2bf）、空类传参（2be27a7）、inline+extern 发射（e9fae35）。下表状态列已按 e06a2b0
> 重新实测更新；「范围 for」「constexpr 多语句」两行由 ⛔ 转 ✅/🟡。requires 表达式（§2.2）在复核时刻已有
> 在途实现（worker-req2），但未合入 HEAD，表中仍记 ⛔。
> **复核（worker-doc4，HEAD 2c474d4，2026-08-03）**：HEAD 已推进到 `2c474d4`（worktree-mxx-work）。
> requires 表达式（§2.2）已由 worker-req4 实现（cpp_parse.c 在途未提交 + test/cpp/requires_min.cc 最小复现），
> 待合入闭环；本表暂记「在途」。新增 4 项基线缺陷（const T& 运算符形参、急切实例化、#elifdef 前组求值、
> 非空类按值返回错乱）见 §2.6，权威基线缺口表在 cpp23-gaps.md §2。门禁：check-chibicc 仍为失败门禁
> （早期 41 COMPILEFAIL 的 sysroot 链接根因已由 6ca4ba1 修复，现不再是 0 通过；worker-chi4 调查，排除出 verify-all.sh）、check-pic-verify riscv64/i386 GOT 已知缺口。

---

## 0. 结论摘要

m++ 已实现 C++20 基础（consteval 折叠、三向比较标量降级、concepts 命名概念 requires-clause、
**range-for 全形态（71fbb35）**）。但 **C++20 完整覆盖仍有 ~10 项真实缺口**（截至 HEAD 2c474d4 复核）。
其中 requires 表达式（P0）已由 worker-req4 在途实现（未合入 HEAD），详见 §2.2。
按"常见现代代码阻塞程度 × 修复成本"排序，Top5：

| # | 缺口 | 证据 | 难度 | 优先级 |
|---|---|---|---|---|
| 1 | **requires 表达式（`requires { ... }`）** | 概念体/requires-clause 内 `requires(T a) { a+a; }` 已由 worker-req4 实现（在途未提交：cpp_parse.c + test/cpp/requires_min.cc），待合入闭环；四类表达式（简单/类型/复合/嵌套）基本就绪 | 高 | P0（在途） |
| 2 | **非类型模板参数（NTTP）** | `template<int N>` / `template<auto N>` 已支持（alice 6fc4d57）；`template<T, T N>` 依赖类型 NTTP **已实现**（grace ef89d22：参数表作用域 + 依赖 NTTP 标记 + 显式实参按参数类型直接解析） | 中高 | P0 ✅ |
| 3 | **consteval 只做折叠、无即时调用强制** | 非常量实参 `sq(v)` 静默降级为运行时调用（标准要求编译错误） | 中 | P1 |
| 4 | **类类型三向比较（defaulted/成员 `operator<=>`/重写）** | `auto operator<=>(...) const = default` / 成员 `int operator<=>(...)` 均报 `unsupported operator for overloading` | 中高 | P1 |
| 5 | **括号/直接列表初始化（C++11 连带）** | `P p(1,2)` / `P p{1,2}` 均失败（聚合无 ctor 时不走直接构造 / 声明符后 `{` 未识别） | 中 | P1 |

> 另：coroutines（co_return/co_await/co_yield）与 modules（export/import/module）**明确不支持**——两者均需
> 重大后端改造（状态机变换 / 分离编译与可达性），建议显式标记为"不支持"而非排期。关键词已 lex（CPP_TCO_*、CPP_TMODULE 等），
> 但解析器无处理分支（实测 `co_return` 报 undeclared identifier）。

---

## 1. 特性总表（全部实测，2026-08-03）

图例：✅ 已支持 ｜ 🟡 部分支持（降级/简化语义）｜ ⛔ 缺口（编译失败或未实现）｜ 🚫 明确不支持

| C++20 特性 | 状态 | 证据（实测命令/文件） | 说明 |
|---|---|---|---|
| consteval（常量折叠） | ✅ | t15_consteval_ok / t34_consteval_member / t60_consteval_sa RUN=0 | 折叠、成员、递归、嵌套、static_assert 全过（e698f37） |
| consteval（即时调用强制） | ✅ | alice 实现 + grace d061167 模板边界验证 | 非常量实参 `sq(v)` 报 "call to consteval function is not a constant expression"（test/cpp/consteval_immediate.cc + consteval_nonconst.neg.cc）；模板边界（consteval 函数模板、constexpr 内常量实参、template<int N>、依赖 NTTP 交叉）由 test/cpp/consteval_tmpl_boundary.cc 闭环 |
| 三向比较 `<=>`（内置标量） | ✅ | t16_spaceship_ok RUN=0 | TSPACESHIP token + 降级 `(a>b)?1:((a<b)?-1:0)`（34d0566；test/cpp/spaceship*.cc） |
| 三向比较（类类型 operator<=>） | ✅ | alice 实现 | cpp_op_mangle 补 TSPACESHIP→'ss'，成员 operator<=> 重载与 a<=>b 调用（test/cpp/spaceship_member.cc） |
| 三向比较（defaulted `= default`） | ⛔ | t09_default_spaceship：`unsupported operator for overloading` | 无 defaulted 成员函数机制 |
| concepts：命名概念 requires-clause | ✅ | t17_concept_req / t51_fn_tmpl_requires_ok RUN=0 | `requires Concept<T>` 函数模板正常；组合/递归已修（8e07d08/b2da695/2755fe3） |
| concepts：`template<Concept T>` 简写 | ⛔ | t01_abbrev_concept：`expected 'typename' or 'class'` | 模板参数解析只认 `typename`/`class`（cpp_parse.c:3713） |
| concepts：缩写函数模板 `f(Concept auto)` | ⛔ | t19_abbrev_fn：`expected ',' or ')' after constructor argument, saw 'auto'` | 参数列表内 Concept+auto 未识别 |
| concepts：requires 表达式 | 🟡 在途（已实现待合入） | test/cpp/requires_min.cc（`static_assert(requires(int a){ a+a; })` 最小复现通过）；t02/t61/t20 历史负例在 worker-req4 在途实现下应转过，待闭环后标注 ✅ | worker-req4 已实现（cpp_parse.c 在途未提交，HEAD 2c474d4 时未合入）；四类表达式语法（简单需求/类型需求/复合需求/嵌套需求）已接通 `cpp_check_constraint` |
| concepts：类模板 + requires-clause | ⛔ | t33_class_tmpl_requires：`unexpected ';' at top-level` | `is_class` 检测在 requires 消费前（cpp_parse.c:3759 vs 3822），类模板被当函数模板缓冲 |
| coroutines | 🚫 | t03_coroutine：`undeclared identifier: co_return` | 关键字已 lex 无解析；需状态机变换，不建议排期 |
| modules | 🚫 | t04_module：`expected declaration or function definition` | `export module foo;` 失败；需分离编译/可达性，不建议排期 |
| 范围 for（含 C++20 初始化语句） | ✅ | e06a2b0 复核：`for (int x : arr)` / `for(auto x : arr)` / `for (int i=0; auto x : arr)` / begin/end 成员全部编译+运行通过 | **71fbb35 已实现**（src/c/parse/stmt.c TFOR +367 行；test/cpp/range_for.cc）；§2.1 由缺口转关闭 |
| constinit | ⛔ | t06_constinit：`expected declaration or function definition` | 关键字未 lex/未识别（cpp_tokens.h 无 CPP_TCONSTINIT） |
| 显式(bool) 条件 explicit | ⛔ | t07_explicit_bool：`no type in struct member declaration` | `explicit(expr)` 形式未处理 |
| 非类型模板参数 NTTP | ✅ | alice 6fc4d57 + grace ef89d22 | `template<int N>`/`template<auto N>` 支持（函数/类模板、显式实参、constexpr 折叠）；`template<T, T N>` 依赖类型 NTTP 已实现（test/cpp/nttp_dep_type.cc），混合显式实参 `f<int,5>` 与多值类模板 `D<2,4>` 已修复 |
| char8_t | ⛔ | t10_char8：`declaration has no type specifier`；复核 `const char* s = u8"abc"` 已可（c-00/e9fae35 后 u8 字面量元素为 `char`，非 `unsigned char`） | 类型系统无 `char8_t` 关键字/类型；u8 字面量现为普通 char 数组 |
| using enum | ⛔ | t11_using_enum：`undeclared identifier: using` | `using enum E;` 未识别 |
| lambda 模板参数 `[]<typename T>` | ⛔ | t12_lambda_tmpl：`expected lambda body` | lambda 头只解析 `[cap](params)` |
| 括号聚合初始化 `P p(a,b)` | ✅ | alice 实现 | 聚合无 ctor 时按成员序初始化（cpp_emit_ctor_call 退化），test/cpp/aggregate_init.cc |
| 直接列表初始化 `P p{a,b}` | ✅ | alice 实现 | 声明符后 `{` 识别为初始化列表走 parseinit，test/cpp/aggregate_init.cc |
| 指定初始化器 `.x =` | ✅ | t13_designated RUN=0 | C++20/C99 共用路径 |
| constexpr 放宽（多语句体/对象成员） | 🟡 | e06a2b0 复核：`constexpr int f(int n){ int s=n; s+=2; return s; }` 已可（dca1620，多语句/局部变量/循环/static constexpr 局部全过，test/cpp/constexpr_body.cc）；但 `S s; s.n=5` 类对象成员访问仍 `not an integer constant expression` | roadmap constexpr 阶段 3 的「对象成员访问」未做（需 mini 内存模型） |
| constexpr lambda | ⛔ | t24_consteval_lambda：`expected lambda body`；复核 `constexpr auto f = [](int){...}` 报 `requires a constant expression initializer` | C++17 缺口连带 |
| constexpr 成员函数在常量对象上调用 | ⛔ | t43_constexpr_member：`no matching member function for 'sq'` | 常量对象 + constexpr 成员未接通 |
| `[[no_unique_address]]` 布局属性 | 🟡 | t31 RUN=0（布局未按属性压缩） | 属性语法接受但无语义（attr.c 静默忽略） |
| 属性（`[[nodiscard]]` 等） | 🟡 | t32 编译通过 | 语法接受、语义忽略（ATTR 表只认 C 风格属性） |

---

## 2. 缺口 Top5 明细

### 2.1 范围 for（P0，难度中）— 含 C++20 初始化语句 — ✅ 已关闭（71fbb35）
- **复核（e06a2b0）**：`for (int x : arr)` / `for(auto x : arr)` / `for (int i = 0; auto x : arr)` / begin/end 成员
  全部编译+运行通过；`test/cpp/range_for.cc`（115 行）覆盖。实现位于 `src/c/parse/stmt.c` TFOR 分支（+367 行）。
- **遗留**：C++20 初始化语句 `for (int i = 0; auto x : arr)` 的 `i` 作用域语义未逐项审计（非阻塞）。

### 2.2 requires 表达式（P0，难度高）
- **证据**：`concept HasPlus = requires(T a) { a + a; };` → `expected declaration or function definition`（t02）；
  `requires { typename T::value; }` 同（t61）；requires-clause 内嵌 `requires(T a){...}` → `undeclared identifier: T`（t20）。
  e06a2b0 复核全复现（`req_expr.cc` / `req_typename.cc` 均编译失败）。
- **根因**：概念体缓冲只按 `;` 收 token，不做 `requires` 子句解析；requires-clause 缓冲器把内层 `requires(...)` 的括号当约束 token 消费掉。
- **修复方向**：完整 requires-expression 语法（简单需求/类型需求/复合需求/嵌套需求）→ 编译为布尔约束表达式接入既有 `cpp_check_constraint`。
- **影响**：concepts 的语义核心（类型检查、成员存在性、约束组合），无它概念只能写 sizeof/比较类简单约束。
- **在途（HEAD 2c474d4）**：worker-req4 已实现（cpp_parse.c 在途未提交 + test/cpp/requires_min.cc 最小复现 `static_assert(requires(int a){ a+a; })` 通过）。四类表达式语法（简单需求/类型需求/复合需求/嵌套需求）已接通 `cpp_check_constraint`。待 worker-req4 合入 HEAD 后，本行状态转 ✅ 并在 §1 总表同步。

### 2.3 非类型模板参数 NTTP（P0，难度中高）
- **证据**：`template <int N>` / `template <typename T, T N>` / `template <auto N>` 一律 `expected 'typename' or 'class' in template parameter list`（t08/t18/t23/t35）。
- **根因**：cpp_template_decl 参数循环硬编码只接受 CPP_TTYPENAME/CPP_TCLASS（cpp_parse.c:3713），且实例化键/推导均按类型参数设计。
- **修复方向**：参数循环扩展（类型/非类型/auto）+ 实例化时按整型常量值绑定 + mangling/缓存键含值。
- **影响**：编译期常量模板（数组维度、`std::integral_constant` 模式）写不出。

### 2.4 consteval 即时调用强制（P1，难度中）
- **证据**：`consteval int sq(int n){...}; int v=7; sq(v);` 编译并通过（t21 RUN=0）。标准要求对非常量实参报错（immediate invocation 必须 constant expression）。
- **现状**：consteval 折叠、递归、成员、static_assert 均正确（e698f37 + t34/t60），唯独"必须在编译期求值"的语义强制缺失——非常量实参静默走运行时路径，掩盖用户错误。
- **修复方向**：consteval 调用点对实参做 is-constant 检查，非常量时报编译错误（而非回退）。注意与既有"简化最小集"行为冲突，需决策。

### 2.5 类类型三向比较（P1，难度中高）
- **证据**：成员 `int operator<=>(const S&) const` → `unsupported operator for overloading`（t22）；`auto operator<=>(...) const = default` 同（t09）。
- **根因**：`cpp_op_mangle` 无 TSPACESHIP 分支（cpp_parse.c:1162-1179），运算符重载注册被拒。
- **修复方向**：① 成员 operator<=> 重载（mangle 编码）；② defaulted 比较生成（按成员序生成 `<`/`==` 等）；③ 重写候选 `a < b` → `(a <=> b) < 0`。三者可分级交付。

### 2.6 新增基线缺陷（HEAD 2c474d4，待修）

以下 4 项为近期实测/交付中确认并已登记。权威的「阻塞 C++20/23 的基线缺口表」见 cpp23-gaps.md §2（其中 D1/D3/D4 已与 §2 同步，避免重复维护）。

| # | 缺陷 | 证据（复现要点） | 状态 | 阻塞 |
|:---:|:-----|:----------------|:-----|:-----|
| D1 | **类类型运算符 `const T&` 形参失败** | `int operator==(const Vec& a, const Vec& b)` / 成员 `bool operator==(const A& o) const` → 「invalid operands to '=='」/「assignment to pointer must be from pointer…」；值形参 `int operator==(Vec a, Vec b)` 可跑。**比 P2468 更宽**：非仅重写候选缺失，引用形参运算符整体失败 | 待修 | 常见 `==`/`<` 写法、P2468 前驱 |
| D2 | **急切实例化未使用成员函数** | m++ 在类模板实例化时**急切实例化全部成员函数**（含未 ODR-used 者）；标准要求仅 ODR-used 才实例化。后果：未使用成员函数体内的错误（未声明名/类型）被误报 | 待修 | 模板健壮性、惰性实例化语义 |
| D3 | **`#elifdef`/`#elifndef` 前组未取误报** | `#if 0 … #elifdef FOO` → 「expected newline after preprocessing directive, saw identifier 'FOO'」；C/C++ 均复现（共享 pp） | worker-pp4 正在修 | P2334 完整 |
| D4 | **P2266 非空类按值返回结果错乱** | `struct V{int n; V(int x):n(x){}}; V make(){V v(7); return v;}` 运行结果 ≠7；空类按值传参/返回已修（2be27a7），非空类仍错乱 | 待修 | P2266 简化隐式移动 |

---

## 3. 部分支持项（语义降级需知悉）

| 特性 | 已支持 | 降级/缺失 | 建议 |
|---|---|---|---|
| consteval | 折叠/递归/成员/static_assert | 无即时调用强制（非常量实参回退运行时） | P1 收紧 |
| `<=>` 标量 | 内置运算符降级为 int 三值 | 无类类型重写；结果类型为 int 非标准排序类型（无 std::strong_ordering） | 接受降级或 P1 补类类型 |
| concepts | 命名概念 requires-clause + 组合/递归 | 无简写语法/无 requires 表达式 | §2.2 补核心 |
| 属性 | 语法接受 `[[...]]` | 无语义（no_unique_address/nodiscard/likely 全忽略） | P2 可选 |

---

## 4. 跨标准连带缺口（阻塞 C++20 且不属于 C++20 新增）

以下为 m++ 更早期标准的缺口，但在实测中直接阻塞 C++20 代码：

| 缺口 | 证据 | 影响 |
|---|---|---|
| ~~range-for 全部缺失~~（C++11） | ~~t30/t42 编译失败~~ → ✅ 已实现（71fbb35） | 已解除；C++20 初始化语句随基础实现一并落地 |
| **直接列表初始化 `T x{...}`**（C++11） | t40 失败，仅 `T x = {...}` 可用；e06a2b0 复核仍失败 | 阻塞 C++20 括号聚合（t14）与 constexpr 对象（t25） |
| **constexpr 对象/成员访问**（C++11 阶段 3） | alice 已实现 + grace 3ae5a04 | constexpr 聚合对象成员访问折叠（mini 内存模型：成员值表 obj+offset->value），`constexpr P p{1,2}` 的 p.a/p.b 可在 static_assert 求值（test/cpp/constexpr_obj_member.cc）；**constexpr 函数返回类对象已实现**（解释器聚合初始化 + 类返回 + 调用结果成员表，test/cpp/constexpr_return_obj.cc；局部 constexpr 类变量从调用初始化亦支持） |

---

## 5. 修复优先级建议（路线图）

```
P0（建议立即排期）
  1. requires 表达式（四类表达式）             —— concepts 语义核心（HEAD 2c474d4 在途：worker-req4 已实现未合入，待闭环后转 ✅）
  2. NTTP（int/枚举值参数起步）               —— 模板编译期常量地基
  3. 类类型运算符 const T& 形参（D1）           —— 比 P2468 更宽，引用形参运算符整体失败，解锁常见 ==/< 写法

P1
  4. consteval 即时调用强制（行为修正）        —— 收紧语义，改动小
  5. 类类型三向比较（成员 operator<=> → defaulted → 重写）—— 分级交付
  6. 类模板 + requires-clause（bug 修）        —— cpp_parse.c 顺序调整，改动小
  7. 非空类按值返回错乱（D4 / P2266）         —— 空类已修（2be27a7），非空类返回路径推广
  8. 急切实例化未使用成员函数（D2）            —— 改为仅 ODR-used 才实例化，模板健壮性
  9. constinit                                —— 关键字 + 编译期初值检查，改动小
 10. char8_t                                  —— 标量类型 + u8"" 字面量类型
 11. 括号/直接列表初始化（C++11 连带）         —— 聚合构造 + 声明符 `{` 解析
 12. #elifdef 前组求值误报（D3）               —— worker-pp4 正在修（共享 pp）

P2（按需）
 13. explicit(bool)  /  lambda 模板参数  /  using enum
 14. constexpr 对象 mini 内存模型（roadmap 阶段 3）
 15. [[no_unique_address]] 等属性语义

明确不支持（标记，不排期）
  - coroutines：需协程状态机变换 + 承诺协议，后端大改造
  - modules：需分离编译、声明可达性、import 解析，前端架构大改
```

---

## 6. 代码位置索引（供修复 worker 参考）

| 缺口 | 位置 |
|---|---|
| 模板参数只收 `typename`/`class` | `src/cpp/parse/cpp_parse.c:3713`（cpp_template_decl 参数循环） |
| 类模板 requires 顺序 bug | `src/cpp/parse/cpp_parse.c:3759`（is_class 检测）早于 `:3822`（requires 消费） |
| 运算符 mangle 无 `<=>` | `src/cpp/parse/cpp_parse.c:1162-1179`（cpp_op_mangle） |
| constinit 未 lex | `include/cpp/cpp_tokens.h`（无 CPP_TCONSTINIT）+ `src/cpp/lex/cpp_scan.c` |
| 属性语义忽略 | `src/c/parse/attr.c`（ATTR 表只认 C 风格，`[[...]]` 静默跳过） |
| ~~range-for~~ | ~~`src/c/parse/stmt.c` TFOR 分支（需加 `:` 处理）~~ → ✅ 已实现（71fbb35） |
| requires 表达式 | `src/cpp/parse/cpp_parse.c` 概念体缓冲 + requires-clause 缓冲器（HEAD 2c474d4 在途：worker-req4，未合入） |
| `operator[]` mangle | `src/cpp/parse/cpp_parse.c:1225`（TLBRACK → "ix"，cfeadf9 已加；da9e2bf 修 const 决议） |
| D1 类类型运算符 const T& 形参 | `src/cpp/parse/cpp_parse.c` 运算符重载注册/调用点的引用形参处理 |
| D2 急切实例化 | `src/cpp/parse/cpp_template_decl` / 类模板实例化路径（应改为惰性，仅 ODR-used 实例化） |
| D3 #elifdef 前组求值 | `src/c/lex/pp.c` 跳过组内 `#elifdef`/`#elifndef` 求值分支（worker-pp4 在修） |
| D4 非空类按值返回 | 返回路径（空类已修 2be27a7，非空聚合返回路径待推广） |

---

## 7. 门禁状态（HEAD 2c474d4 实测）

| 门禁 | 状态 | 说明 |
|:-----|:-----|:-----|
| check-chibicc | **RED 门禁（未通过，排除出 verify-all.sh）** | chibicc 社区套件兼容性仍为失败门禁。早期 41 项 COMPILEFAIL 的根因是 sysroot 路径推导错误（`6ca4ba1` 已修复「cannot find -lc-meuos」链接失败），修复后套件已不再是 0 通过，但仍有大量标准/C conformance 缺陷（见 cpp23-gaps.md §2 基线缺口）未闭环。**worker-chi4 调查中**；`test/community/chibicc/results.log` 为 gitignore 易变产物（取决于构建状态，含在途未提交改动时数字会漂移），最新一次运行约 `PASS=9 / RUNFAIL=6 / COMPILEFAIL=26`，门禁整体仍判定失败并排除出 verify-all.sh。 |
| check-pic-verify | **FAIL（已知缺口）** | riscv64 不发射 GOT 序列（`%got_pcrel_hi` 缺失）、i386 缺 `@GOT(` 序列；x86_64/aarch64 OK。未纳入 verify-all.sh（脚本内标注 known gap）。 |
| verify-all.sh | 17/17 PASS | HEAD 2c474d4 全量门禁通过（含自举 check-sysroot-static 与 MIR/LIR 双路径矩阵）；上述两门禁因已知缺口被显式排除。 |

---

## 附录：实测探测文件

全部临时文件位于 `/tmp/cpp20gaps/`（已按约束不留未提交文件）：
t01~t08 第一批、t09~t18 第二批（含 4 项基线确认）、t19~t26、t30~t35、t40~t43、t50/t51 对照组、t60/t61。
复跑命令：`m++ --specs=host -o out tNN.cc && ./out`。
e06a2b0 复核用临时文件位于 `/tmp/cppgap-audit/`（smoke/if_consteval/range_for_init/range_for_beginend/nttp_int/
nttp_auto/req_expr/req_typename/tmpl_concept_abbrev/abbrev_fn/cls_tmpl_requires/paren_agg/brace_init/constexpr_obj/
constexpr_static_local/constexpr_ref/constexpr_arr/designated/nodiscard/bogus_attr/udl/coroutine/module/consteval_immediate/
constinit/constinit2/char8/u8_type/u8_sz/spaceship_member/spaceship_default/eq_*/op_eq_*/…），未留库内文件。
