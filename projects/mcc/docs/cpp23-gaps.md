# m++ C++23 特性缺口（cpp23-gaps.md）

> 状态：调研文档（worker-cpp23，2026-08-03）。**只调研不改代码**。
> 方法：对照 C++23 core language 特性清单，逐项用 `./m++ --specs=host` 实际编译+运行验证（临时 .cc 在 /tmp，未留库内文件）；不确定项做隔离验证排除前置特性干扰。
> 前置：`docs/cpp-roadmap.md`（2026-08-02，C++98~20 全量完成）。目标：C++98~23 覆盖收官。
> 约定：状态 = ✅ 已支持 / 🟡 部分支持 / 🔴 缺口（「缺口」指 C++23 项本身或前置基线缺失导致不可用）。

## 0. 结论摘要

- **已支持 5 项**：#warning（P2437）、#elifdef/#elifndef（P2334）、复合语句末尾标签（P2324）、consteval 传播（P2564，宽松求值器天然支持）、lambda 省略括号的 `-> ret` 子集（P1102 部分）。
- **部分支持 2 项**：P1102（`[x] -> int {...}` 无括号 OK，`[x] noexcept`/`[x]() mutable` 因 noexcept/mutable 基线缺失失败）、P1169 static `operator()`（static 限定被忽略，按非静态成员处理）。
- **C++23 纯缺口约 10 项**：`if consteval`（P1938）、多维 `operator[]`（P2128，被 `operator[]` 重载基线缺失阻塞）、deducing this（P0847）、`auto(x)`（P0849）、分隔转义 `\u{...}`/`\x{...}`（P2290）、命名转义 `\N{...}`（P2361）、扩展浮点后缀 f16/f32/f64/f128（P1467）、`[[assume]]`（P1774，属性一律被静默忽略）、`if constexpr` 窄化转 bool（P1401）、init 语句 alias（P2360）。
- **被基线阻塞无法独立评估**：P2266（类按值返回/RVO 基线缺陷）、P2242/P2647/P2280/P2448（constexpr 函数体仅单 return 限制）、P2718（range-for 缺失）、P2582（继承构造缺失）、P2468（重写相等候选缺失）、P2513（char8_t 关键字缺失，C++20 项）。
- **最高杠杆的阻塞缺口**（非 C++23 本身，但卡住一批 C++23 特性）：constexpr 函数体只支持 `{ return <expr>; }`，多语句/局部变量/数组/if-constexpr-参数全部失败；`operator[]` 重载完全缺失。

## 1. C++23 特性核实表（实测）

### 1.1 已支持

| C++23 特性 | 证据（实测编译+运行） | 备注 |
|:-----------|:---------------------|:-----|
| `#warning`（P2437） | `#warning "msg"` 编译通过、输出警告不中断 | 共享预处理器 src/c/lex/pp.c:1250；test/c23/warning_directive.c |
| `#elifdef`/`#elifndef`（P2334） | `#if 0 … #elifdef FOO … #else` 正确分支、运行通过 | 共享预处理器；test/c23/elifdef.c |
| 复合语句末尾标签（P2324） | `if (n>0) { n++; lab: }` 编译+运行通过 | — |
| consteval 传播（P2564） | `consteval int id(int); constexpr int f(int x){return id(x);}` + `constexpr int a=f(7)` 运行正确 | token 回放求值器宽松，天然满足 |
| lambda 省略括号（P1102 子集） | `[x] -> int { return x; }` 编译+运行通过 | 见 1.2 限制 |

### 1.2 部分支持

| C++23 特性 | 现象 | 证据 | 难度 | 优先级 |
|:-----------|:-----|:-----|:----:|:-----:|
| lambda 省略括号（P1102） | `[x] -> int {...}` ✅；`[x] noexcept {...}` / `[x]() mutable {...}` ❌ | 后两者报「expected lambda body」，根因是 **noexcept/mutable 限定符基线缺失**（`int f() noexcept` 同样报错） | 低 | 中 |
| static `operator()`（P1169） | 语法接受但 **static 限定被忽略**：static `operator()` 内访问非静态成员 `n` 不报错；`F::operator()(21)` 限定直调报「undeclared identifier: F」 | 隔离测试 static_op_nomember/static_op_real | 中 | 中 |

### 1.3 缺口（纯 C++23 项）

| C++23 特性 | 现象 | 证据（错误信息） | 阻塞项 | 难度 | 优先级 |
|:-----------|:-----|:----------------|:-------|:----:|:-----:|
| `if consteval`（P1938） | `if consteval {…} else {…}` 编译失败 | 「expected '(' after 'if', saw identifier 'consteval'」 | —（consteval 关键字与求值器已具备，可复用） | 中 | **高** |
| 多维下标 `operator[]`（P2128） | `int operator[](int,int)` 编译失败 | 「unsupported operator for overloading」——**单参 `operator[]` 也完全缺失** | cpp_parse.c:1159 mangle 表无 `[]` 条目 | 低（补 mangle `[]`→"ix" + 多参即可） | **高** |
| 显式对象参数 deducing this（P0847） | `int get(this X& self)` 编译失败 | 「undeclared identifier: this」（参数位 `this` 未识别） | 参考 aburiscript parser/parser_record.cpp:6374 | 中高 | 中 |
| `auto(x)` 退化拷贝（P0849） | `auto b = auto(r);` 编译失败 | 「expected primary expression」 | 复用 auto 推导 + 退化规则 | 低-中 | 中 |
| 分隔转义 `\u{...}`/`\x{...}`（P2290） | `"\u{41}"` 编译失败 | 「invalid escape sequence」（scan.c:216 escape() 字符串内不支持 \u） | UCN UTF-8 编码逻辑已有（scan.c:133-160） | 低-中 | 中 |
| 命名转义 `\N{...}`（P2361） | `"\N{LATIN SMALL LETTER A}"` 编译失败 | 「invalid escape sequence」 | 同上 | 低-中 | 低 |
| 扩展浮点后缀 f16/f32/f64/f128/bf16（P1467） | `1.0f32` 编译失败 | 「invalid floating constant suffix 'f32'」；`_Float32` 类型亦未声明 | 需 _FloatN 类型或映射现有 float/double | 低-中 | 低 |
| `[[assume(expr)]]`（P1774） | 编译通过但**属性一律被静默忽略** | `[[assume(x>0 && undefined)]]`、`[[totally_bogus_attr(1,2,3)]]` 均通过；C 模式 `[[deprecated]]` 使用也无警告 | 属性语义（含 C 模式）整体未实现 | 中 | 低 |
| `if constexpr` 窄化转 bool（P1401） | `if constexpr (p)`（p 为指针常量）编译失败 | 「if constexpr condition is not a constant expression」 | 指针常量表达式支持有限 | 中 | 低 |
| init 语句中 alias（P2360） | `if (using T=int; …)` 编译失败 | 「undeclared identifier: using」 | — | 中 | 低 |

### 1.4 被基线阻塞（无法独立评估的 C++23 项）

| C++23 特性 | 阻塞的基线缺口 | 隔离证据 |
|:-----------|:--------------|:---------|
| 简化隐式移动（P2266） | 类按值返回/RVO 缺陷 | `struct V{int n; V(int x):n(x){} }; V make(){V v(7); return v;}` 运行结果 ≠7；带析构/堆指针版本直接段错误 139 |
| constexpr 放宽（P2242 非字面量变量、P2647 static constexpr 变量、P2280 引用、P2448 放宽） | constexpr 函数体仅 `{ return <expr>; }` | `constexpr int f(int n){ int s = n*2; return s; }` → 「undeclared identifier: s」；`if constexpr (n>0)`（n 为 constexpr 参数）→ 「not a constant expression」 |
| range-for 临时生命周期（P2718） | range-based for 完全缺失 | `for (int x : a)` → 「expected ',' or ';' after declarator」 |
| 继承构造 CTAD（P2582） | 继承构造 `using B::B` 缺失 | `struct D : B { using B::B; };` → 「no type in struct member declaration」 |
| 重写相等候选（P2468） | 重载决议无反向候选 | `struct A{…}; 5 == a` → 「invalid operands to '=='」 |
| char8_t 兼容（P2513） | char8_t 关键字缺失（C++20 项） | `const char8_t* s = u8"…"` → 「declaration has no type specifier」；实测 `u8"a"` 为 `unsigned char[2]`，`const char* s = u8"abc"` 类型不兼容 |

## 2. 阻塞 C++23 的基线缺口（C++11~20，实测）

| 基线缺口 | 现象（实测） | 影响 |
|:---------|:------------|:-----|
| `operator[]` 重载完全缺失 | `int operator[](int i)` → 「unsupported operator for overloading」；cpp_parse.c:1159 mangle 表仅 17 个运算符 | 阻塞 P2128 及大量 C++11 容器代码 |
| `noexcept` 限定符缺失 | `int f() noexcept` → 「expected ',' or ';' after declarator」 | 阻塞 P1102 子集 |
| `extern "C"` 连接说明缺失 | `extern "C" int f(…);` 与 `extern "C" {…}` → 「declaration has no type specifier」 | C++98 基线，C 互操作常用 |
| range-based for 缺失 | `for (int x : a)` → 语法错误 | C++11 基线，阻塞 P2718 |
| 引用数组参数缺失 | `int sum(int(&a)[3])` → 「no type in parameter declaration」（roadmap ns_limits 已知） | 阻塞 P0388 |
| 类按值返回缺陷 | 返回类对象运行结果错乱/段错误 | C++98 基线，阻塞 P2266 |
| 继承构造缺失 | `using B::B` → 语法错误 | C++11 基线，阻塞 P2582 |
| init-capture / 引用捕获 | `[n=42]` → 「cannot capture variable 'n'」；`[&x]` → 「not supported yet」 | C++14/C++11 基线（引用捕获 roadmap 已注明） |
| constexpr 函数体单 return | 局部变量/多语句/数组均失败 | C++14 放宽未落地，阻塞 P2242 等一批 |
| 属性语义全忽略 | `[[deprecated]]`/`[[nodiscard]]`/任意名属性均无诊断 | C++11 基线，阻塞 P1774 |
| 其他 | `char8_t`、UDL `operator""`、协程（co_return 等）、`inline namespace`、用户自定义 `operator<=>`（内置 `<=>` 已有）均缺 | C++20 基线 |

## 3. 实施建议（按价值/难度）

1. **`if consteval`（高优先，中难度）**：`consteval` 关键字与 `cpp_constexpr_eval` 求值器已具备，只需 `if consteval` 解析 + 常量上下文标记，让所选分支走求值器。纯 C++23 项中价值最高。
2. **`operator[]` 重载（高优先，低难度）**：cpp_parse.c:1159 mangle 表补 `[]`→`"ix"`（aburiscript abi/mangle.cpp:246 同款），成员/自由运算符路径复用现有参数解析——多参数天然支持；表达式层 `a[i,j]` 逗号多实参调用语法需在 C++ 表达式层处理。一举解锁 C++11 `operator[]` + C++23 P2128。
3. **constexpr 函数体多语句（高优先，中高难度）**：token 回放缓冲（`cpp_buffer_constexpr_body`）从「单 return 表达式」扩展为「局部变量声明 + return 语句序列」，解锁 P2242/P2647/P2280/P2448 一整批 C++23 特性。这是杠杆最高的阻塞项。
4. **`auto(x)` 退化拷贝（中优先，低-中难度）**：表达式层 `auto(` 识别 → 复用 auto 推导 + 退化（去引用/顶层 cv）→ 构造。参考 aburiscript helpers/auto_type_utils.cpp。
5. **分隔/命名转义（中优先，低难度）**：scan.c:216 escape() 补 `\u{…}`/`\U{…}`/`\x{…}`/`\N{…}`，码点→UTF-8 编码复用 scan.c:133-160 的 UCN 逻辑。
6. **deducing this（中优先，中高难度）**：方法参数首位 `this X& self` 特殊解析：绑定为显式对象参数（等价非静态成员函数的 this），参考 aburiscript parser/parser_record.cpp:6374。
7. **static `operator()`（中优先，中难度）**：is_static 已传（cpp_parse.c:1549），补 static 语义校验（禁访问非静态成员）+ `F::operator()` 限定调用语法。
8. **char8_t（中优先，中难度）**：加 `char8_t` 关键字/类型，u8 字面量类型化为 `const char8_t[]`（当前是 `unsigned char[]`）。
9. **f16/f32/f64 后缀（低优先，低难度）**：number() 接受新后缀，映射现有 float/double（不引入 _FloatN 真类型）。
10. **`[[assume]]`（低优先，中难度）**：属性语义整体落地（至少解析已知属性名并校验表达式），或仅记录为「已解析忽略」。

## 4. 参考源

| 特性 | 参考源（实际存在） |
|:-----|:------------------|
| operator[] mangle | `reference/aburiscript/abi/mangle.cpp:246`（`{"[]", "ix"}`） |
| deducing this | `reference/aburiscript/parser/parser_record.cpp:6374`（explicit object parameter lowering） |
| consteval / if consteval | `reference/aburiscript/parser/parser_cxx.cpp:10596-10630`（consteval 关键字处理）、`consteval_engine.cpp` |
| auto / 退化 | `reference/aburiscript/helpers/auto_type_utils.cpp` |
| constexpr 求值 | `reference/aburiscript/constexpr/consteval_engine.cpp` |
| 重载候选/重写相等 | `reference/aburiscript/collect/collect_overload.cpp` |

## 5. 验证方法

```sh
cd projects/mcc
./m++ --specs=host -o /tmp/t <临时.cc> && /tmp/t    # exit 0 = 支持
```

所有验证用例为临时文件（/tmp/cpp23-*），未提交任何业务代码；工作树中其他 worker 的改动（Makefile、test/cpp/pending 迁移等）均未触碰。
