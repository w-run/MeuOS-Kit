# m++ C++23 特性缺口（cpp23-gaps.md）

> 状态：调研文档（worker-cpp23，2026-08-03）。**只调研不改代码**。
> 方法：对照 C++23 core language 特性清单，逐项用 `./m++ --specs=host` 实际编译+运行验证（临时 .cc 在 /tmp，未留库内文件）；不确定项做隔离验证排除前置特性干扰。
> 前置：`docs/cpp-roadmap.md`（2026-08-02，C++98~20 全量完成）。目标：C++98~23 覆盖收官。
> 约定：状态 = ✅ 已支持 / 🟡 部分支持 / 🔴 缺口（「缺口」指 C++23 项本身或前置基线缺失导致不可用）。
> **复核（worker-req，2026-08-03）**：HEAD 已推进到 e06a2b0。原缺口中的 `if consteval`（55499d6）、多维
> `operator[]`（cfeadf9 + da9e2bf）、deducing this（bea068d）、constexpr 多语句体（dca1620）、range-for
> （71fbb35）均已落地。下表 1.1/1.3/1.4/2 已按 e06a2b0 重新实测更新。
> **复核（worker-doc4，HEAD 2c474d4，2026-08-03）**：HEAD 已推进到 `2c474d4`。P2290/P2361/P1467 已实现闭环
> （0025f1b 字符串/字面量路径 + 99d4a54 标识符 UCN 路径 + af905db 窄字面量 `\x{}` 截断），从 §1.3 缺口转入
> §1.1 已支持；C23 三差距（__VA_OPT__/__has_c_attribute/constexpr 函数求值）由 226d31e 闭环（见 c23-review.md）。
> 新增 4 项基线缺陷（D1 const T& 运算符形参、D2 急切实例化、D3 #elifdef 前组求值、D4 非空类按值返回）见 §2 与 §1.4。
> 门禁：check-chibicc PASS=0/COMPILEFAIL=41（worker-chi4 调查）、check-pic-verify riscv64/i386 GOT 已知缺口。

## 0. 结论摘要

- **已支持 5 项**：#warning（P2437）、#elifdef/#elifndef（P2334）、复合语句末尾标签（P2324）、consteval 传播（P2564，宽松求值器天然支持）、lambda 省略括号的 `-> ret` 子集（P1102 部分）。
- **e06a2b0 复核后新增支持 5 项**：`if consteval`（P1938，55499d6，含 `if !consteval`/嵌套/模板）、多维 `operator[]`（P2128，cfeadf9 + const 决议 da9e2bf）、deducing this（P0847，bea068d，引用形式 X&/const X&/X&&）、constexpr 多语句体（dca1620，局部变量/循环/分支/static constexpr 局部，P2242/P2647 地基）、range-for（71fbb35，含 C++20 init-statement 与 begin/end 成员）。
- **HEAD 2c474d4 新增支持 3 项（P2290/P2361/P1467，0025f1b + 99d4a54 + af905db）**：分隔/命名转义、`\u{...}`/`\U{...}`/`\x{...}`/`\N{NAME}`（字符串与标识符路径）、扩展浮点后缀 f16/f32/f64/f128/bf16（映射现有 float/double，`_FloatN` 真类型未引入）。
- **部分支持 2 项**：P1102（`[x] -> int {...}` 无括号 OK，`[x] noexcept`/`[x]() mutable` 因 noexcept/mutable 基线缺失失败）、P1169 static `operator()`（static 限定被忽略，按非静态成员处理）。
- **C++23 纯缺口约 4 项**（P2290/P2361/P1467 已闭环）：`auto(x)`（P0849）、`[[assume]]`（P1774，属性一律被静默忽略）、`if constexpr` 窄化转 bool（P1401）、init 语句 alias（P2360）。
- **被基线阻塞无法独立评估**：P2266（**非空类**按值返回/RVO 基线缺陷，见 §1.4 D4）、P2242/P2280/P2448 的**引用/数组/类对象成员**部分（多语句/局部变量已通）、P2582（继承构造缺失）、P2468（重写相等候选缺失；且 `operator==` 带 `const T&` 形参时整体失败，见 §2 D1）、P2513（char8_t 关键字缺失，C++20 项）、急切实例化（D2，模板健壮性）。
- **最高杠杆的阻塞缺口**（非 C++23 本身，但卡住一批 C++23 特性）：`operator==`/`operator<=>` 类类型重载中的 **const 引用形参** 解析失败（D1，比 P2468 更宽）；类按值返回缺陷（非空类结果错乱，D4）。

## 1. C++23 特性核实表（实测）

### 1.1 已支持

| C++23 特性 | 证据（实测编译+运行） | 备注 |
|:-----------|:---------------------|:-----|
| `#warning`（P2437） | `#warning "msg"` 编译通过、输出警告不中断 | 共享预处理器 src/c/lex/pp.c:1250；test/c23/warning_directive.c |
| `#elifdef`/`#elifndef`（P2334） | `#ifdef X … #elifdef Y … #else` 正确分支、运行通过 | 共享预处理器；test/c23/elifdef.c。⚠️ 边界：位于被跳过的 `#if 0` 组内时 `#elifdef FOO` 报「expected newline after preprocessing directive」（C/C++ 均复现，共享 pp 限制） |
| 复合语句末尾标签（P2324） | `if (n>0) { n++; lab: }` 编译+运行通过 | — |
| consteval 传播（P2564） | `consteval int id(int); constexpr int f(int x){return id(x);}` + `constexpr int a=f(7)` 运行正确 | token 回放求值器宽松，天然满足 |
| lambda 省略括号（P1102 子集） | `[x] -> int { return x; }` 编译+运行通过 | 见 1.2 限制 |
| `if consteval`（P1938） | 复验 `constexpr int csum(int n){ if consteval {…} else {…} }`：常量实参折叠取 consteval 分支、运行时实参取 else 分支；`if !consteval`/嵌套/static_assert 全过 | **55499d6**；test/cpp/if_consteval.cc（含普通函数恒走 else 分支） |
| 多维下标 `operator[]`（P2128） | 复验 `int operator[](int i, int j)` 编译+运行通过 | **cfeadf9**（mangle TLBRACK→"ix" cpp_parse.c:1225）；**da9e2bf** 修 const 决议；test/cpp/multidim_index.cc |
| 显式对象参数 deducing this（P0847） | 复验 `int get(this Counter& self)` / `this const Counter&` / `this Counter&&` 值类别重载全过 | **bea068d**（引用形式）；test/cpp/deducing_this.cc |
| constexpr 多语句体（P2242/P2647 地基） | 复验 `constexpr int add2(int n){ int s=n; s+=2; return s; }` 与循环/分支/static constexpr 局部/嵌套调用全过 | **dca1620**；test/cpp/constexpr_body.cc。剩余：引用/数组/类对象成员仍失败（见 1.4） |
| range-based for（P2718 + C++11 基线） | 复验 `for (int x : arr)` / `for (auto x : arr)` / `for (int i=0; auto x : arr)` / begin/end 成员全过 | **71fbb35**；src/c/parse/stmt.c TFOR；test/cpp/range_for.cc |
| 分隔转义 `\u{...}`/`\U{...}`/`\x{...}`（P2290） | `"\u{41}"`/`"\U{1F600}"`/`"\x{1F}"` 编译+运行通过；经典 `\uXXXX`/`\UXXXXXXXX` 原行为保留 | **0025f1b**（escape/字符串路径）+ **99d4a54**（标识符路径）；src/c/lex/scan.c：delimited()/namedval()。check-c23/c99/c11/cpp 全 PASS |
| 命名转义 `\N{NAME}`（P2361） | `"\N{LATIN CAPITAL LETTER A}"` 编译+运行通过 | **0025f1b**（scan.c namedval 命名查表）+ **99d4a54**；空分隔/越界/未终止错误诊断齐备 |
| 扩展浮点后缀 f16/f32/f64/f128/bf16（P1467） | `1.0f32`/`0.5bf16` 编译通过（映射现有 float/double；`f64`/`f128`→double，`f16`/`f32`/`bf16`→float） | **0025f1b**（scan.c number() 后缀表）；`_FloatN` 真类型未引入。验证：check-c23/c99/c11/cpp 全 PASS |

### 1.2 部分支持

| C++23 特性 | 现象 | 证据 | 难度 | 优先级 |
|:-----------|:-----|:-----|:----:|:-----:|
| lambda 省略括号（P1102） | `[x] -> int {...}` ✅（e06a2b0 复验通过）；`[x] noexcept {...}` / `[x]() mutable {...}` ❌ | 后两者报「expected lambda body」，根因是 **noexcept/mutable 限定符基线缺失**（`int f() noexcept` 同样报错） | 低 | 中 |
| static `operator()`（P1169） | 语法接受但 **static 限定被忽略**：static `operator()` 内访问非静态成员 `n` 不报错；`F::operator()(21)` 限定直调报「undeclared identifier: F」；普通 `f(21)` 调用可跑 | e06a2b0 复验（static_op.cc 通过 / static_op_member.cc / static_op_qual.cc 复现） | 中 | 中 |

### 1.3 缺口（纯 C++23 项）

| C++23 特性 | 现象 | 证据（错误信息） | 阻塞项 | 难度 | 优先级 |
|:-----------|:-----|:----------------|:-------|:----:|:-----:|
| `auto(x)` 退化拷贝（P0849） | `auto b = auto(r);` 编译失败 | 「expected primary expression」 | 复用 auto 推导 + 退化规则 | 低-中 | 中 |
| `[[assume(expr)]]`（P1774） | 编译通过但**属性一律被静默忽略** | `[[assume(x>0 && undefined)]]`、`[[totally_bogus_attr(1,2,3)]]` 均通过且无诊断；e06a2b0 复验 | 属性语义（含 C 模式）整体未实现 | 中 | 低 |
| `if constexpr` 窄化转 bool（P1401） | `if constexpr (p)`（p 为指针常量）编译失败 | 「if constexpr condition is not a constant expression」 | 指针常量表达式支持有限 | 中 | 低 |
| init 语句中 alias（P2360） | `if (using T=int; …)` 编译失败 | 「undeclared identifier: using」 | — | 中 | 低 |

### 1.4 被基线阻塞（无法独立评估的 C++23 项）

| C++23 特性 | 阻塞的基线缺口 | 隔离证据 |
|:-----------|:--------------|:---------|
| 简化隐式移动（P2266） | **非空类**按值返回/RVO 缺陷（D4，待修） | `struct V{int n; V(int x):n(x){} }; V make(){V v(7); return v;}` 运行结果 ≠7（e06a2b0 复验 exit=1）。注：空类按值传参/返回已修（2be27a7），非空类仍错乱 |
| constexpr 放宽（P2242 非字面量变量、P2280 引用、P2448 放宽） | constexpr 函数体**引用/数组/类对象成员**支持缺失 | ✅ 多语句/局部变量/循环/分支/static constexpr 局部已通（dca1620，test/cpp/constexpr_body.cc，P2647 静态局部复验过）；剩余：`const int& r = v` → 「requires a constant expression initializer」；constexpr 体局部数组 → 「expected primary expression」；`S s; s.n=5` → 「not an integer constant expression」 |
| ~~range-for 临时生命周期（P2718）~~ | ~~range-based for 完全缺失~~ | ✅ **71fbb35 已实现**（含 C++20 init-statement、begin/end 成员，test/cpp/range_for.cc） |
| 继承构造 CTAD（P2582） | 继承构造 `using B::B` 缺失 | `struct D : B { using B::B; };` → 「no type in struct member declaration」（e06a2b0 复验） |
| 重写相等候选（P2468） | 重载决议无反向候选；且 **`operator==` 带 `const T&` 形参时整体失败** | `struct A{…}; a == a`（成员/自由 operator==，`const A&` 形参）→ 「invalid operands to '=='」/「assignment to pointer must be from pointer…」；**值形参 `int operator==(Vec a, Vec b)` 可跑**。`5 == a` 反转候选更无支持 |
| char8_t 兼容（P2513） | char8_t 关键字缺失（C++20 项） | `const char8_t* s = u8"…"` → 「declaration has no type specifier」。**u8 字面量元素现为 `char`**（e9fae35/c-00 后，`const char* s = u8"abc"` 可跑；原文档「unsigned char[2]」说法已过时） |
| 急切实例化（D2，待修） | m++ 类模板实例化时**急切实例化全部成员函数**（含未 ODR-used 者）；标准仅要求 ODR-used 才实例化 | 未使用成员函数体内的错误（未声明名/类型）被误报；`struct S { void unused() { undeclared; } };` 在 S 实例化时即报错（预期不应） |

## 2. 阻塞 C++23 的基线缺口（C++11~20，实测）

| 基线缺口 | 现象（实测） | 影响 |
|:---------|:------------|:-----|
| ~~`operator[]` 重载完全缺失~~ | ~~`int operator[](int i)` → 「unsupported operator for overloading」~~ | ✅ **cfeadf9 已实现**（mangle TLBRACK→"ix"）+ const 决议 da9e2bf；阻塞 P2128 已解除 |
| `operator==` 等类类型重载的 **`const T&` 形参**（**D1，待修**） | `int operator==(const Vec& a, const Vec& b)` / 成员 `bool operator==(const A& o) const` → 「invalid operands to '=='」/「assignment to pointer must be from pointer…」；值形参版本可跑 | 新发现（e06a2b0）：引用形参重载解析缺陷，**比 P2468 更宽**（非仅重写候选缺失，引用形参运算符整体失败），阻塞常见 `==`/`<` 写法与 P2468 |
| `noexcept` 限定符缺失 | `int f() noexcept` → 「expected ',' or ';' after declarator」 | 阻塞 P1102 子集 |
| `extern "C"` 连接说明缺失 | `extern "C" int f(…);` 与 `extern "C" {…}` → 「declaration has no type specifier」 | C++98 基线，C 互操作常用 |
| ~~range-based for 缺失~~ | ~~`for (int x : a)` → 语法错误~~ | ✅ **71fbb35 已实现**；阻塞 P2718 已解除 |
| 引用数组参数缺失 | `int sum(int(&a)[3])` → 「no type in parameter declaration」（roadmap ns_limits 已知） | 阻塞 P0388 |
| 类按值返回缺陷（**D4，待修**） | 返回非空类对象运行结果错乱（exit≠期望）；空类已修（2be27a7） | C++98 基线，阻塞 P2266（简化隐式移动） |
| 继承构造缺失 | `using B::B` → 语法错误 | C++11 基线，阻塞 P2582 |
| 急切实例化未使用成员函数（**D2，待修**） | 类模板实例化时全部成员函数被急切实例化（含未 ODR-used 者），未使用成员函数体内错误被误报 | C++11 模板语义偏差；标准仅要求 ODR-used 才实例化（惰性） |
| init-capture / 引用捕获 | `[n=42]` → 「cannot capture variable 'n'」；`[&x]` → 「not supported yet」（e06a2b0 复验） | C++14/C++11 基线（引用捕获 roadmap 已注明） |
| ~~constexpr 函数体单 return~~ | ~~局部变量/多语句/数组均失败~~ | ✅ **dca1620 已实现**多语句/局部变量/循环；剩余引用/数组/类对象（见 1.4） |
| 属性语义全忽略 | `[[deprecated]]`/`[[nodiscard]]`/任意名属性均无诊断（e06a2b0 复验） | C++11 基线，阻塞 P1774 |
| 其他 | `char8_t`、UDL `operator""`（→「unsupported operator for overloading」）、协程（co_return 等）、`inline namespace`（→「declaration has no type specifier」）、用户自定义 `operator<=>`（内置 `<=>` 已有）均缺 | C++20 基线 |
| `#elifdef`/`#elifndef` 求值边界（**D3**） | 前组未取（`#if 0 … #elifdef FOO`）时「expected newline after preprocessing directive, saw identifier 'FOO'」；前组已取（`#if 1 … #elifdef FOO`）正常 | 共享 pp（C/C++ 均复现）；P2334 已支持但求值路径有缺口。**worker-pp4 正在修**（src/c/lex/pp.c 跳过组内求值分支） |

## 3. 实施建议（按价值/难度）

1. **`operator==`/`<` 类类型重载的 `const T&` 形参（D1，高优先，中难度）**：e06a2b0 新发现，HEAD 2c474d4 仍待修。`int operator==(const Vec& a, const Vec& b)` 报「assignment to pointer…」，值形参可跑——定位运算符重载注册/调用点的引用形参处理。解锁常见比较运算符 + P2468 前驱。
2. **非空类按值返回缺陷（D4，高优先，中高难度）**：非空类 `V make(){V v(7); return v;}` 结果错乱（exit≠0）；空类已修（2be27a7），需推广到一般聚合返回路径。阻塞 P2266。
3. **`auto(x)` 退化拷贝（中优先，低-中难度）**：表达式层 `auto(` 识别 → 复用 auto 推导 + 退化（去引用/顶层 cv）→ 构造。参考 aburiscript helpers/auto_type_utils.cpp。
4. **constexpr 体引用/数组/类对象（中优先，中高难度）**：dca1620 已覆盖多语句/局部/循环；补引用绑定与类对象 mini 内存模型后，P2242/P2280/P2448 可收官。
5. **急切实例化（D2，中优先，中难度）**：类模板成员函数改为仅 ODR-used 才实例化（惰性），避免未使用成员函数体内错误误报。
6. **`#elifdef` 求值缺口（D3，低优先，低难度）**：共享 pp 在跳过组内求值 `#elifdef` 时误报；修复后 P2334 才算完整。**worker-pp4 正在修**。
7. **char8_t（中优先，中难度）**：加 `char8_t` 关键字/类型，u8 字面量类型化为 `const char8_t[]`（当前已是 char 数组，比文档初版记录更进一步）。
8. **`[[assume]]`（低优先，中难度）**：属性语义整体落地（至少解析已知属性名并校验表达式），或仅记录为「已解析忽略」。

> 已关闭项（不再排期）：if consteval（55499d6）、多维 operator[]（cfeadf9/da9e2bf）、deducing this（bea068d）、constexpr 多语句体（dca1620）、range-for（71fbb35）、分隔/命名转义 + 扩展浮点后缀（P2290/P2361/P1467，0025f1b + 99d4a54 + af905db）。

## 4. 参考源

| 特性 | 参考源（实际存在） |
|:-----|:------------------|
| operator[] mangle | `reference/aburiscript/abi/mangle.cpp:246`（`{"[]", "ix"}`）——✅ 已按此实现（cfeadf9） |
| deducing this | `reference/aburiscript/parser/parser_record.cpp:6374`（explicit object parameter lowering）——✅ 已实现（bea068d） |
| consteval / if consteval | `reference/aburiscript/parser/parser_cxx.cpp:10596-10630`（consteval 关键字处理）、`consteval_engine.cpp`——✅ if consteval 已实现（55499d6） |
| auto / 退化 | `reference/aburiscript/helpers/auto_type_utils.cpp` |
| constexpr 求值 | `reference/aburiscript/constexpr/consteval_engine.cpp`——✅ 多语句体已实现（dca1620） |
| 重载候选/重写相等 | `reference/aburiscript/collect/collect_overload.cpp`（P2468 仍缺） |

## 5. 验证方法

```sh
cd projects/mcc
./m++ --specs=host -o /tmp/t <临时.cc> && /tmp/t    # exit 0 = 支持
```

所有验证用例为临时文件（/tmp/cpp23-*），未提交任何业务代码；工作树中其他 worker 的改动（Makefile、test/cpp/pending 迁移等）均未触碰。
e06a2b0 复核用例位于 `/tmp/cppgap-audit/`（auto_paren/esc_brace/esc_named/esc_ucn/f32_suffix/assume/ifconstexpr_ptr/
if_alias/static_op*/lambda_*/noexcept_fn/extern_c/byval_ret/byval_dtor/inherit_ctor/eq_*/op_eq_*/ref_arr_param/init_capture/
ref_capture/elif_min*/char8/u8_*/consteval_*/inline_ns/udl/…），未留库内文件。

## 6. 门禁状态（HEAD 2c474d4）

| 门禁 | 状态 | 说明 |
|:-----|:-----|:-----|
| check-chibicc | **PASS=0 / RUNFAIL=0 / COMPILEFAIL=41（共 41）** | chibicc 社区套件兼容性全灭（test/community/chibicc/results.log 现状）。失败形态含「cannot find -lc-meuos」（sysroot 未安装导致链接失败）与标准/C conformance 缺陷（§2 基线缺口）。**worker-chi4 调查中**，未纳入 verify-all.sh。 |
| check-pic-verify | **FAIL（已知缺口）** | riscv64 不发射 GOT 序列（`%got_pcrel_hi` 缺失）、i386 缺 `@GOT(` 序列；x86_64/aarch64 OK。未纳入 verify-all.sh（脚本内标注 known gap）。 |
| verify-all.sh | 17/17 PASS | HEAD 2c474d4 全量门禁通过（含自举 check-sysroot-static 与 MIR/LIR 双路径矩阵）；上述两门禁因已知缺口被显式排除。 |
