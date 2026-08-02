# m++ C++20 剩余特性缺口路线图（cpp20-gaps.md）

> 状态：调研文档（worker-cpp20 产出，2026-08-03）。只调研+实测，不改业务代码。
> 基线：HEAD 6142ec5（worktree-mxx-work），`m++ 0.1.0`，`--specs=host` 实测。
> 目的：对照 C++20 core language（非标准库）特性清单，逐项核实 m++ 支持情况，输出缺口/证据/难度/优先级。

---

## 0. 结论摘要

m++ 已实现 C++20 三项骨架（consteval 折叠、三向比较标量降级、concepts 命名概念 requires-clause），
但 **C++20 完整覆盖仍有 12+ 项真实缺口**。按"常见现代代码阻塞程度 × 修复成本"排序，Top5：

| # | 缺口 | 证据 | 难度 | 优先级 |
|---|---|---|---|---|
| 1 | **范围 for 全部缺失（含 C++20 初始化语句）** | `for (int x : arr)` / `for(auto x : arr)` / `for (int i=0; auto x : arr)` 均编译失败 | 中 | P0 |
| 2 | **requires 表达式（`requires { ... }`）** | 概念体/requires-clause 内 `requires(T a) { a+a; }` 均报错 | 高 | P0 |
| 3 | **非类型模板参数（NTTP）** | `template<int N>` / `template<auto N>` / `template<T, T N>` 均报 `expected 'typename' or 'class'` | 中高 | P0 |
| 4 | **consteval 只做折叠、无即时调用强制** | 非常量实参 `sq(v)` 静默降级为运行时调用（标准要求编译错误） | 中 | P1 |
| 5 | **类类型三向比较（defaulted/成员 `operator<=>`/重写）** | `auto operator<=>(...) const = default` / 成员 `int operator<=>(...)` 均报 `unsupported operator for overloading` | 中高 | P1 |

> 另：coroutines（co_return/co_await/co_yield）与 modules（export/import/module）**明确不支持**——两者均需
> 重大后端改造（状态机变换 / 分离编译与可达性），建议显式标记为"不支持"而非排期。关键词已 lex（CPP_TCO_*、CPP_TMODULE 等），
> 但解析器无处理分支（实测 `co_return` 报 undeclared identifier）。

---

## 1. 特性总表（全部实测，2026-08-03）

图例：✅ 已支持 ｜ 🟡 部分支持（降级/简化语义）｜ ⛔ 缺口（编译失败或未实现）｜ 🚫 明确不支持

| C++20 特性 | 状态 | 证据（实测命令/文件） | 说明 |
|---|---|---|---|
| consteval（常量折叠） | ✅ | t15_consteval_ok / t34_consteval_member / t60_consteval_sa RUN=0 | 折叠、成员、递归、嵌套、static_assert 全过（e698f37） |
| consteval（即时调用强制） | 🟡 | t21_consteval_runtime RUN=0 | 非常量实参 `sq(v)` 静默降级运行时调用，标准要求编译错误（文档承认的最小集行为） |
| 三向比较 `<=>`（内置标量） | ✅ | t16_spaceship_ok RUN=0 | TSPACESHIP token + 降级 `(a>b)?1:((a<b)?-1:0)`（34d0566；test/cpp/spaceship*.cc） |
| 三向比较（类类型 operator<=>） | ⛔ | t22_synth_rel：`unsupported operator for overloading` | `cpp_op_mangle` 无 TSPACESHIP 分支（cpp_parse.c:1162-1179） |
| 三向比较（defaulted `= default`） | ⛔ | t09_default_spaceship：`unsupported operator for overloading` | 无 defaulted 成员函数机制 |
| concepts：命名概念 requires-clause | ✅ | t17_concept_req / t51_fn_tmpl_requires_ok RUN=0 | `requires Concept<T>` 函数模板正常；组合/递归已修（8e07d08/b2da695/2755fe3） |
| concepts：`template<Concept T>` 简写 | ⛔ | t01_abbrev_concept：`expected 'typename' or 'class'` | 模板参数解析只认 `typename`/`class`（cpp_parse.c:3713） |
| concepts：缩写函数模板 `f(Concept auto)` | ⛔ | t19_abbrev_fn：`expected ',' or ')' after constructor argument, saw 'auto'` | 参数列表内 Concept+auto 未识别 |
| concepts：requires 表达式 | ⛔ | t02_requires_expr / t61_concept_typename_req / t20_nested_requires | 概念体 `requires(T a){...}`、`requires{typename T::value;}`、requires-clause 内嵌 `requires` 全失败 |
| concepts：类模板 + requires-clause | ⛔ | t33_class_tmpl_requires：`unexpected ';' at top-level` | `is_class` 检测在 requires 消费前（cpp_parse.c:3759 vs 3822），类模板被当函数模板缓冲 |
| coroutines | 🚫 | t03_coroutine：`undeclared identifier: co_return` | 关键字已 lex 无解析；需状态机变换，不建议排期 |
| modules | 🚫 | t04_module：`expected declaration or function definition` | `export module foo;` 失败；需分离编译/可达性，不建议排期 |
| 范围 for 初始化语句 | ⛔ | t05_range_for_init：`expected primary expression` | 且**基础 range-for 本身全缺**（见 §4） |
| constinit | ⛔ | t06_constinit：`expected declaration or function definition` | 关键字未 lex/未识别（cpp_tokens.h 无 CPP_TCONSTINIT） |
| 显式(bool) 条件 explicit | ⛔ | t07_explicit_bool：`no type in struct member declaration` | `explicit(expr)` 形式未处理 |
| 非类型模板参数 NTTP | ⛔ | t08/t18/t35：`template<int N>` `expected 'typename' or 'class'`；t23 `template<auto N>` 同 | 模板参数循环只收类型参数 |
| char8_t | ⛔ | t10_char8：`declaration has no type specifier` | 类型系统无 char8_t；`u8"hi"` 仍作普通字符串 |
| using enum | ⛔ | t11_using_enum：`undeclared identifier: using` | `using enum E;` 未识别 |
| lambda 模板参数 `[]<typename T>` | ⛔ | t12_lambda_tmpl：`expected lambda body` | lambda 头只解析 `[cap](params)` |
| 括号聚合初始化 `P p(a,b)` | ⛔ | t14_paren_agg：`no matching constructor for object 'p'` | 聚合无 ctor 时不走直接构造 |
| 直接列表初始化 `P p{a,b}` | ⛔ | t40_brace_init：`expected ',' or ';' after declarator, saw '{'` | 仅 `P p = {a,b}`（copy-list-init）可用；C++11 缺口连带 |
| 指定初始化器 `.x =` | ✅ | t13_designated RUN=0 | C++20/C99 共用路径 |
| constexpr 放宽（对象成员访问） | ⛔ | t25_constexpr_obj：`expected ',' or ';' after declarator, saw '{'` | roadmap constexpr 阶段 3 未做（需 mini 内存模型） |
| constexpr lambda | ⛔ | t24_consteval_lambda：`expected lambda body` | C++17 缺口连带 |
| constexpr 成员函数在常量对象上调用 | ⛔ | t43_constexpr_member：`no matching member function for 'sq'` | 常量对象 + constexpr 成员未接通 |
| `[[no_unique_address]]` 布局属性 | 🟡 | t31 RUN=0（布局未按属性压缩） | 属性语法接受但无语义（attr.c 静默忽略） |
| 属性（`[[nodiscard]]` 等） | 🟡 | t32 编译通过 | 语法接受、语义忽略（ATTR 表只认 C 风格属性） |

---

## 2. 缺口 Top5 明细

### 2.1 范围 for（P0，难度中）— 含 C++20 初始化语句
- **证据**：
  - 基础 C++11 range-for：`for (auto x : arr)` → `'auto' variable requires an initializer`；`for (int x : arr)` → `expected ',' or ';' after declarator, saw ':'`（t30/t42）
  - C++20 初始化语句：`for (int i = 0; auto x : arr)` → `expected primary expression`（t05）
  - 现有 test/cpp/ 无任何 range-for 用例（`grep "for (.* : "` 零命中）——功能从未实现
- **修复方向**：stmt() 的 TFOR 分支识别 `:` 语法，降级为迭代循环（m++ 现有数组/指针循环即可，先支持数组 + begin/end 函数对可选）。C++20 init-statement 只是 `for(init; decl : range)` 的头部扩展。
- **影响**：现代 C++ 代码最常用的遍历语法，阻塞面极大。**P0**。

### 2.2 requires 表达式（P0，难度高）
- **证据**：`concept HasPlus = requires(T a) { a + a; };` → `expected declaration or function definition`（t02）；
  `requires { typename T::value; }` 同（t61）；requires-clause 内嵌 `requires(T a){...}` → `undeclared identifier: T`（t20）。
- **根因**：概念体缓冲只按 `;` 收 token，不做 `requires` 子句解析；requires-clause 缓冲器把内层 `requires(...)` 的括号当约束 token 消费掉。
- **修复方向**：完整 requires-expression 语法（简单需求/类型需求/复合需求/嵌套需求）→ 编译为布尔约束表达式接入既有 `cpp_check_constraint`。
- **影响**：concepts 的语义核心（类型检查、成员存在性、约束组合），无它概念只能写 sizeof/比较类简单约束。

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
| **range-for 全部缺失**（C++11） | t30/t42 编译失败 | 阻塞 C++20 初始化语句（§2.1），先补 C++11 基础 |
| **直接列表初始化 `T x{...}`**（C++11） | t40 失败，仅 `T x = {...}` 可用 | 阻塞 C++20 括号聚合（t14）与 constexpr 对象（t25） |
| **constexpr 对象/成员访问**（C++11 阶段 3） | t25/t43 失败 | 阻塞 C++20 constexpr 放宽 |

---

## 5. 修复优先级建议（路线图）

```
P0（建议立即排期）
  1. 基础 range-for + C++20 初始化语句        —— 语法最常用，改动集中 stmt.c TFOR
  2. requires 表达式（简单需求起步）           —— concepts 语义核心
  3. NTTP（int/枚举值参数起步）               —— 模板编译期常量地基

P1
  4. consteval 即时调用强制（行为修正）        —— 收紧语义，改动小
  5. 类类型三向比较（成员 operator<=> → defaulted → 重写）—— 分级交付
  6. 类模板 + requires-clause（bug 修）        —— cpp_parse.c 顺序调整，改动小
  7. constinit                                —— 关键字 + 编译期初值检查，改动小
  8. char8_t                                  —— 标量类型 + u8"" 字面量类型

P2（按需）
  9. explicit(bool)  /  lambda 模板参数  /  using enum  /  括号聚合初始化
 10. 直接列表初始化（C++11 连带）
 11. constexpr 对象 mini 内存模型（roadmap 阶段 3）
 12. [[no_unique_address]] 等属性语义

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
| range-for | `src/c/parse/stmt.c` TFOR 分支（需加 `:` 处理） |
| requires 表达式 | `src/cpp/parse/cpp_parse.c` 概念体缓冲 + requires-clause 缓冲器 |

---

## 附录：实测探测文件

全部临时文件位于 `/tmp/cpp20gaps/`（已按约束不留未提交文件）：
t01~t08 第一批、t09~t18 第二批（含 4 项基线确认）、t19~t26、t30~t35、t40~t43、t50/t51 对照组、t60/t61。
复跑命令：`m++ --specs=host -o out tNN.cc && ./out`。
