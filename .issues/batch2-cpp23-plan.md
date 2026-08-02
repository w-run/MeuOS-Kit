# m++ C++23 三缺口实现方案（batch2-cpp23-plan.md）

> 状态：调研方案文档（worker-cpp23，2026-08-03，批次 2）。**只调研不改代码**。
> 工作树：worktree-mxx-work @ c54b363（干净）。验证用例在 /tmp/cpp23-b2-probe/（临时，不入库）。
> 前置：`docs/cpp23-gaps.md`（批次 1 缺口表）、`docs/cpp-roadmap.md`（C++98~20 全量完成）。
> 目标：给实施 worker 可直接开工的三缺口实现方案。三个缺口 = **constexpr 体放宽（P2242 及前置）、if consteval（P1938）、deducing this（P0847）**。

---

## 0. 结论摘要（先看这里）

| 缺口 | 现状（实测错误签名） | 难度 | 依赖 | 建议 |
|:-----|:-------------------|:----:|:-----|:-----|
| G1 constexpr 体放宽 | `int s = n; s += 2; return s;` → 「undeclared identifier: s」；`if (n>3) return…` → 「expected ';' after constexpr return expression」 | **高** | 无（独立于 G2/G3） | 第 2 步做，核心大件 |
| G2 if consteval | `if consteval` → 「expected '(' after 'if', saw identifier 'consteval'」 | 期1 低 / 期2 中 | 期2 依赖 G1 的语句解释器 | 第 1 步做期1（快赢） |
| G3 deducing this | `int get(this X& self)` → 「undeclared identifier: this」（参数位 `this` 未识别） | 中高 | 无 | 第 3 步做（独立子系统） |

**推荐实施顺序**：G2 期1（if consteval 于普通函数，~50 行独立）→ G1（constexpr 语句解释器，最高杠杆）→ G3（deducing this 基础形态）→ G2 期2（if consteval 于 constexpr 函数体，依赖 G1）。
**关键机制事实**（三缺口共同地基，已实测确认）：
- constexpr 函数体**运行时定义已支持任意语句**（多语句/循环体编译通过、运行正确）；失败仅发生在**常量求值路径**（`cpp_constexpr_eval` 假定体为 `{ return <expr>; }`）。
- 常量求值由 `eval()` 的 EXPRCALL 分支（src/c/sema/eval.c:232-242）无条件触发：**constexpr 函数 + 全整型常量实参**即折叠，无论结果是否需要编译期常量。因此多语句 constexpr 函数只要被以常量实参调用就必然命中求值器 → 求值器必须能解释语句序列。
- 区分「编译期求值中」与「普通运行期解析」已有现成判别量：`g_cpp_cexpr_depth > 0`（cpp_constexpr_eval 在 5541/5555 自增/自减）。if consteval 的分支选择直接复用此判别。
- m++ 无「引用/值类别/this」隔离层，但已有完整地基：`cpp_skip_branch()`（5678，token 级跳分支）、`g_cpp_method.this_decl`（方法体内 this）、引用 = 带 `isref` 的指针（使用自动解引用，src/c/sema/type.c:245）。

---

## 1. G1：constexpr 体放宽（C++14 核心 + C++23 P2242）

### 1.1 现状

- **运行时定义 OK**：`cpp_buffer_constexpr_body`（cpp_parse.c:5414）把整个 `{...}` 缓冲 + 回放，`stmt()` 照常解析运行时定义。实测 `constexpr int f(int n){ int s=n; s+=2; return s; }` 运行时调用编译通过、结果正确（/tmp/cpp23-b2-probe/d4）。
- **常量求值只认 `{ return <expr>; }`**：`cpp_constexpr_eval`（cpp_parse.c:5483）回放体 token 后：
  - `next(); /* { */` → `next(); /* return */` → `next(); /* 表达式起点 */` → `e = expr(tmp)` → `expect(TSEMICOLON)`。
  - 首语句是声明 → 把 `int` 当 `return` 吞掉后 expr 从 `s` 开始 → 「undeclared identifier: s」。
  - 首语句是 `if` → 「expected ';' after constexpr return expression, saw 'return'」。
- 参数绑定：临时 scope（`mkscope(&filescope)`）中每个形参 `mkdecl(..., DECLCONST, ..., QUALCONST)` 值 `args[i]`（5533-5540）。
- 求值递归深度上限 `g_cpp_cexpr_depth` 64（5528）。
- **`if constexpr` 于常量求值的现状**：`if constexpr (sizeof(T)==4)` 在模板里工作（runtime 解析路径）；但 `if constexpr (n > 0)`（n 为函数形参）被拒（「not a constant expression」）——**这是标准正确行为**（C++17 要求 if constexpr 条件为常量表达式，形参不是），**不是缺陷**，方案不针对它。constexpr 函数内应写普通 `if (n > 0)`。

### 1.2 标准要求

- **C++14 relaxed constexpr（前置核心）**：函数体可含局部变量声明、if/else、while/for/do 循环、多 return、复合语句；调用需可常量求值（满足则折叠，否则允许运行期调用——m++ 一贯的宽松降级）。
- **C++23 P2242**：允许非字面量类型局部变量（只要不在常量求值中 odr-use）、标签与 goto（函数仍可声明 constexpr）。
- **C++23 P2647（team-lead 提到的「static 变量」）**：constexpr 函数内可定义 `static constexpr` 变量。
- **局部类**：C++23 允许 constexpr 函数体含局部类定义——但 m++ 块作用域类本身未支持，属额外基线。

### 1.3 实现方案

**核心思路**：把 `cpp_constexpr_eval` 从「解析单 return 表达式」扩展为「解释语句序列」的迷你解释器（复用既有 token 回放 + 临时 scope 机制，参考 aburiscript `consteval_engine.cpp` 的 InterpreterSession/EvalMemory 设计，但 m++ 是 token 回放架构，只借算法不搬代码）。

**改动文件/函数**（全在 `src/cpp/parse/cpp_parse.c`，eval.c 挂载点不动）：

1. 新增语句解释器一族（取代 5547-5553 的固定序列）：
   - `static int cpp_cexpr_stmt(struct scope *tmp)`——解释一条语句，返回状态码：`CEVAL_OK`（继续）、`CEVAL_RET`（已折叠出结果，带值）、`CEVAL_FAIL`（遇非常量，放弃求值返回 NULL 降级运行期）。
   - 语句分派（按 token kind）：
     - `{...}` 复合 → 递归解释内部序列。
     - 声明 `int s = <init>;` / `constexpr int s = …` / `int s;` → 用 `exprassign`/`parseinit` 解析初始化，`eval()` 折叠；折叠成 `EXPRCONST` 整型 → `mkdecl("s", DECLCONST, inttype, QUALCONST)` + `u.enumconst` 值入 tmp scope（与形参绑定同法，5534-5539）；折叠失败 → `CEVAL_FAIL`。
     - `return <expr>;` → 折叠 → 携带结果返回 `CEVAL_RET`。
     - `if (cond) A else B` → 折叠 cond，递归解释选中分支（A 或 B）；`else if` 链复用。
     - `while (cond) stmt` / `for (init; cond; step) stmt` / `do stmt while(cond);` → 折叠 cond 循环解释体，**加迭代步数上限**（如 100000）防编译期挂死（沿用 5528 深度上限的防挂思想）。
     - 表达式语句 `expr;` → 折叠丢弃（副作用不求值）。
     - 其他（`break`/`continue`/`goto`/局部类/static 声明等）→ `CEVAL_FAIL`（降级运行期；本轮不解释）。
   - `cpp_constexpr_eval` 改为：回放体 token 后调用 `cpp_cexpr_stmt` 序列；遇到 `CEVAL_RET` 取其值建 `EXPRCONST` 返回；全程 `CEVAL_FAIL` → 返回 NULL（运行期调用保留）。
2. **静态变量 P2647**（进阶子项）：运行时体已能解析 `static constexpr int t[] = {…}` 吗？需实测。若运行时 OK，常量求值路径需「数组常量表」支持（`DECLCONST` 只存单个整型）——本轮建议**降级运行期**（不求值含数组的调用），记为 P2647 求值面待后续。
3. **`if consteval` 于求值路径**（G2 期2 联动，见 §2.3）：语句解释器遇到 `if consteval` 分支选择用 `g_cpp_cexpr_depth > 0` 判别——求值回放时恒为真 → 取 consteval 分支。

### 1.4 测试用例设计（新 test/cpp/constexpr_body.cc，接入 check-cpp-func）

| 用例 | 期望 |
|:-----|:-----|
| `constexpr int f(int n){ int s=n; s+=2; return s; }` + `constexpr int a=f(5)` | a==7 |
| `if (n>3) return n+1; return n-1;` | f(5)==6、f(2)==1 |
| `for` 循环求和 `sum(4)` | 10 |
| `while` 循环计数 | 正确 |
| 嵌套调用：外层 constexpr 调内层多语句 constexpr | 折叠正确 |
| 运行期调用 `f(g)`（g 为非编译期值） | 走运行时定义，结果正确（降级不误） |
| 负向（.neg.cc）：局部变量初始化含不可折叠值（如 `int s = g;` 后常量求值） | 编译报错或降级（按实现选） |
| P2242 运行时接受：体内含 `goto`/标签/非字面量局部变量（不常量求值） | 编译通过、运行正确 |

### 1.5 风险点

- **无限循环挂死**：解释器必须带迭代步数上限 + 保留 `g_cpp_cexpr_depth` 64 上限。
- **求值器与运行期重复解析**：body token 会被求值回放 + 运行期解析各走一遍，`cpp_cexpr_stmt` 必须在临时 scope 上自洽（成员/全局标识符仍应能解析——注意 tmp scope 以 `&filescope` 为父，5533）。
- **降级判定一致性**：`CEVAL_FAIL` 要安全回退——求值过程已消费了 token 流（tokpush 回放），失败时确保不残留污染运行期解析（回放机制本身已隔离，但需验证）。
- **数组/对象状态**：本轮只解释整型标量；任何数组、指针、对象成员求值一律降级（避免 P2242 承诺过度）。
- **与 G2/G3 同文件并发**：cpp_parse.c 是共享热点文件，实施 worker 需与 G2/G3 分工文件区域（G1 在 5483 附近求值区）。

---

## 2. G2：if consteval（P1938）

### 2.1 现状

- `if constexpr` 已支持：src/c/parse/stmt.c:526 TIF 分支——`g_lang==1 && tok.kind == TCONSTEXPR` → 调 `cpp_if_constexpr`（cpp_parse.c:5579）。机制：`eval()` 折叠条件，选中分支经 `stmt()` 解析、未选分支 `cpp_skip_branch()` token 级跳过。
- `if consteval` 失败：stmt.c 只认 `constexpr` 关键字（TCONSTEXPR）；`consteval` 是标识符（C 词法给 TIDENT，C++ 层 `cpp_tok_kind()` 重分类为 CPP_TCONSTEVAL），TIF 分支不识别 → 「expected '(' after 'if'」。
- `consteval` 函数已工作：src/c/parse/specs.c:75-81 `typequal()` 按标识符名 "consteval" 映射为 `QUALCONST|QUALCONSTEXPR`，复用 constexpr 机制。
- `cpp_skip_branch()`（cpp_parse.c:5678）可复用。

### 2.2 标准要求（P1938）

- `if consteval {A} else {B}`：语句处于**常量求值上下文**时取 A，**运行期上下文**取 B。
- `if !consteval`：取反。
- 被弃分支**仍需语法良构**（不实例化模板，但语法要合法）——minimal 实现可 token 级跳过，但跳过逻辑必须括号配平（`cpp_skip_branch` 已保证）；标准更严格（需诊断），记为已知降级。

### 2.3 实现方案

**期 1（独立，快赢，~50 行）**——`if consteval` 于普通（非 constexpr）函数：
1. stmt.c TIF 分支（526 后）增加识别：
   ```c
   if (g_lang == 1 && tok.kind >= TIDENT &&
       strcmp(tokenstr(tok.kind), "consteval") == 0) → consteval_if = true; next();
   /* 也识别 `if ! consteval`：tok.kind == TLNOT 后跟 consteval 标识符 */
   ```
   注意 `if !consteval` 在 `!` 之后 `consteval` 仍是标识符。
2. 新函数 `cpp_if_consteval(struct func *f, struct scope *s, bool negate)`（cpp_parse.c，仿 `cpp_if_constexpr`）：`expect(TLPAREN)`、`expr(s)`（条件表达式按 P1938 被忽略/仅作解析）、`expect(TRPAREN)`，然后按 `g_cpp_cexpr_depth > 0` 判别：
   - 普通运行期解析（depth==0）：`if consteval` 取 **else 分支**解析（`cpp_skip_branch` 跳过 then），`if !consteval` 取 then；无 else 则仅跳过。
   - 常量求值回放（depth>0）：取 consteval 分支。
3. 期 1 不要求求值器改动：普通函数体内 `if consteval` 恒为运行期 → 恒取 else 分支（但用 depth 判别写未来正确）。

**期 2（依赖 G1）**——`if consteval` 于 constexpr 函数体：
- 依赖 G1 的 `cpp_cexpr_stmt` 语句解释器把 `if consteval` 作为一条语句处理：常量求值回放时（depth>0）取 consteval 分支；运行期定义解析时取 else 分支。同一判别量天然成立。
- 常见形态 `constexpr int f(int n){ if consteval { return n*2; } else { return n+100; } }` 因此要求 G1 先行。

**改动文件**：`src/c/parse/stmt.c`（TIF 分支）、`src/cpp/parse/cpp_parse.c`（`cpp_if_consteval` + G1 的语句解释器支持）。

### 2.4 测试用例设计（test/cpp/if_consteval.cc + 负向）

| 用例 | 期望 |
|:-----|:-----|
| `constexpr int f(int n){ if consteval { return n*2; } else { return n+100; } }`：`constexpr int a=f(10)`；`int v=5; f(v)` | a==20（consteval 分支）、f(v)==105（else 分支） |
| `if !consteval { … } else { … }`：语义取反 | consteval 调用走 else、运行期走 then |
| 非 constexpr 函数 `int f(){ if consteval { return 1; } else { return 2; } }` | f()==2 |
| 无 else：`if consteval { … }`（运行期则无操作） | 编译+运行正确 |
| 负向（.neg.cc）：`if consteval` 无圆括号、`if ! consteval` 缺 `!` | 编译拒绝 |

### 2.5 风险点

- **`if constexpr` 与 `if consteval` 词法区分**：`constexpr`（TCONSTEXPR）与 `consteval`（标识符）token 不同，判别逻辑要按 kind 分支，别混。
- **被弃分支良构性**：minimal 实现 token 跳过（配平），与标准「需语法良构」有差距——记录为已知降级，测试只覆盖功能分支。
- **`if !consteval` 的 `!` 处理**：stmt.c 需在 `if` 后先探测 `!`（TLNOT）再探测 consteval，避免与按位非混淆。

---

## 3. G3：deducing this（P0847）

### 3.1 现状

- 非静态成员函数 mtype = `Class_method(Class *this, 显式形参…)`（`cpp_define_method` cpp_parse.c:1429；隐式 this 构造 1537-1544，`mkdecl("this", DECLOBJECT, X*)`）。
- 调用降级：`obj.f(args)` / `obj[idx]` → 解析 `Class_method<参数编码>`，`&obj` 前置为第一个实参（成员调用路径，见 `cpp_subscript_call` 1329-1342 同构；重载分辨率用 `cpp_mangled_name_args` 2977 的 R/V 值类别标记 + 参数类型编码）。
- 方法体内 `this`：`g_cpp_method.this_decl`（296-307 `cpp_this_expr`）；裸成员名 → `(*this).name`（`cpp_member_ident` 318）。
- 静态成员：mangle 尾缀 "S"、无 this。
- 参数解析：declarator.c func 分支（185-250）`parameter(s)` 逐个解析；`this X& self` 中 `this` 是 TIDENT → 被当标识符/类型 → 「undeclared identifier: this」。
- 引用 = 带 `isref` 的指针，使用自动解引用（type.c:245、init.c:264）。

### 3.2 标准要求（P0847）

- `void f(this X& self)`：显式对象参数，非静态成员；`self` 绑定对象；cv/值类别由声明类型决定（`X&` 左值可变、`const X&` 左值只读、`X&&` 右值、`X` 按值拷贝）。
- **deducing 形态**：`this auto& self` / `template<typename Self> void f(this Self& self)` 推导对象类型（CRTP、值类别统一重载）。本轮建议只做**非 deducing 基础形态**（`this X& self`、`this const X& self`、`this X&& self`、`this X self`），deducing 形态依赖模板机制另记。
- 调用：`x.f()`、`X{}.f()`、指针 `p->f()` 均可。

### 3.3 实现方案

**改动点**：

1. **参数解析拦截**（src/c/parse/declarator.c func 分支，222-235 参数循环）：当 `g_lang==1` 且是**首个参数**且 `cpp_tok_kind() == CPP_TTHIS`（tokenstr "this"）时，解析显式对象参数：
   - `next()` 消费 `this`；随后按普通声明解析类型+名（`X& self`，复用 declspecs + declarator）。
   - 约束校验：必须是首个参数；类型必须是包围类（引用或按值）；存到全局（如 `g_cpp_explicit_obj`，含 decl + 声明类型），并**不出现在普通形参链**（或标记后由 cpp_define_method 特判）。
2. **cpp_define_method 集成**（cpp_parse.c:1429）：当方法有显式对象参数：
   - **不合成隐式 `Class *this`**；对象参数转成 mtype 的 param[0]：`X& self` → `X&`（isref 指针）——**调用降级前置 `&obj` 天然匹配**（引用=指针）；`const X&` → const 指针；`X&&` → isrref 指针（绑定临时，需临时物化支持，roadmap 移动语义已有）；`X self` 按值 → 对象拷贝参数（调用方传拷贝，ABI 走既有按值路径）。
   - `is_const` 由对象参数 cv 推导（const X& → 方法 const，mangle 尾缀 "K" 一致）。
   - `thisd`（供 `cpp_member_ident`/`cpp_this_expr` 用）= 对象参数转指针（`mkpointertype`），`g_cpp_method.this_decl` 指向它 → 体内裸成员访问走既有 `(*this).member` 路径。
   - `self` 注册进方法体作用域：`mkdecl("self", DECLOBJECT, X&(isref), …)` → 使用自动解引用，`self.n` 直接可用。
3. **mangle/重载**：对象参数并入 `cpp_define_method` 的 mangle 编码（1507-1511 显式参数类型编码）+ 调用侧 `cpp_mangled_name_args` 的 R/V 值类别标记已天然覆盖 `x.f()`（lvalue→R）vs `X{}.f()`（rvalue→V）。静态 "S" 尾缀逻辑保持。
4. **调用降级**：既有成员调用路径无需改（`&obj` 前置 + 参数编码匹配）；需验证 rvalue 对象 `X{}.f()` 的临时物化路径（roadmap 移动语义已实现临时绑定）。
5. **deducing 形态**（`this auto& self`）：模板实参推导，依赖既有模板 token 回放——**本轮不做**，记为后续（可借 aburiscript `collect_calls.cpp:4069` explicit object argument 思路）。

**改动文件**：`src/c/parse/declarator.c`（参数循环）、`src/cpp/parse/cpp_parse.c`（`cpp_define_method`、新增 `g_cpp_explicit_obj`、thisd 绑定）。调用降级路径（postfix TPERIOD）验证为主。

### 3.4 测试用例设计（test/cpp/deducing_this.cc）

| 用例 | 期望 |
|:-----|:-----|
| `struct X { int n; int get(this X& self){ return self.n; } };` `x.get()` | 7 |
| `int get(this const X& self)`：const 对象调用、非 const 重载区分 | const 对象走 const 重载 |
| `void bump(this X&& self)`：临时对象调用；左值对象调用应解析到 `X&` 重载 | 值类别重载正确 |
| 体内裸成员访问：`this X& self` 内直接 `n`（无 self. 前缀） | `(*self).n` 正确 |
| `self` 引用语义：`self.n = 5` 写回原对象 | 原对象被修改 |
| 静态成员共存：同类的 static 方法不受影响 | 正常 |
| 负向（.neg.cc）：`this` 出现在非首参；`this` 后类型非包围类；无 `this` 的普通方法正常 | 仅非法形态被拒 |

### 3.5 风险点

- **参数解析拦截侵入 declarator.c**：C 前端共享文件，改动要严格 `g_lang==1` 守卫 + 仅首参 + 仅 `this` 标识符，避免影响 C 模式。
- **对象参数与隐式 this 的等价性**：`self` 与 `this` 必须指向同一对象（self 是 `*this` 的引用别名）——实现上用同一 `this_decl` 派生，避免两套对象表示漂移。
- **值类别/const 语义**：`X&` vs `X&&` vs `const X&` 重载共存时，调用侧 R/V 标记与参数编码要匹配 mangle（复用既有机制，但需验证临时对象路径）。
- **按值对象参数 `this X self`**：拷贝语义（需拷贝构造），依赖类按值传参正确性——**注意 cpp23-gaps.md §2 记录「类按值返回基线缺陷」**，若类按值传参也有缺陷，按值形态需先确认基线。建议本轮只做引用形态（`X&`/`const X&`/`X&&`），按值形态延后。

---

## 4. 依赖关系与推荐实施顺序

### 4.1 依赖图

```
G2 期1（if consteval @普通函数）   ── 无依赖
G1（constexpr 语句解释器）          ── 无依赖（但为 G2 期2 提供地基）
G3（deducing this 引用形态）        ── 无依赖（共享 declarator.c/cpp_parse.c 文件注意并发）
G2 期2（if consteval @constexpr 体）── 依赖 G1
```

### 4.2 推荐顺序（按价值/依赖/风险）

1. **G2 期1（低难度，快赢）**：stmt.c + cpp_parse.c 各 ~25 行，独立可交付；为 G1 引入 `g_cpp_cexpr_depth` 判别先例。
2. **G1（高难度，最高杠杆）**：解锁 C++14 核心 constexpr + P2242/P2647/P2280/P2448 一批 C++23 特性，并使 G2 期2 成立。实施 worker 应参考 aburiscript consteval_engine.cpp 的语句解释语义（只借算法）。
3. **G3（中高难度，独立子系统）**：基础引用形态（`X&`/`const X&`/`X&&`），按值形态延后（受类按值传参基线影响）。
4. **G2 期2（中难度）**：搭 G1 的语句解释器，把 `if consteval` 作为语句处理。

### 4.3 文件分工（避免并发冲突）

- G2 期1：`src/c/parse/stmt.c`（TIF 分支）+ `src/cpp/parse/cpp_parse.c`（`cpp_if_consteval`，可放 5579 `cpp_if_constexpr` 旁）。
- G1：`src/cpp/parse/cpp_parse.c`（5483 求值区扩展为 `cpp_cexpr_stmt` 一族）。
- G3：`src/c/parse/declarator.c` + `src/cpp/parse/cpp_parse.c`（`cpp_define_method` 1429 区域 + 新增全局）。
- 三者同涉 cpp_parse.c：**G2/G1 区（5483 附近）与 G3 区（1429 附近）不同函数段**，可并行但提交必须文件级 add + commit 前 `git status` 自查（沿用团队 0802 广播纪律）。

---

## 5. 参考源索引

| 特性 | 参考源（实际存在） |
|:-----|:------------------|
| constexpr 语句解释 | `reference/aburiscript/constexpr/consteval_engine.cpp`（InterpreterSession 287、EvalMemory；AST 解释器，借语句/控制流语义） |
| if consteval | `reference/cplusplus/packages/cxx-frontend/src/AST.ts:4427`（ConstevalIfStatementAST 节点）；`reference/aburiscript/parser/parser_cxx.cpp:10596-10630`（consteval 关键字） |
| deducing this | `reference/aburiscript/parser/parser_record.cpp:6374`（explicit object parameter lowering）；`reference/aburiscript/collect/collect_calls.cpp:4069` |
| m++ 现状 | `projects/mcc/src/cpp/parse/cpp_parse.c`（5483 求值器 / 1429 方法定义 / 5678 skip_branch）、`src/c/parse/stmt.c:526`（if 分支）、`src/c/parse/declarator.c:185-250`（函数形参）、`src/c/parse/specs.c:75`（consteval 关键字） |

---

## 6. 验证方法

```sh
cd projects/mcc
./m++ --specs=host -o /tmp/t <新测试.cc> && /tmp/t    # exit 0 = 通过
make check-cpp-func   # 新测试入 test/cpp/ 后全量回归
```

本方案为调研产出；验证探针在 /tmp/cpp23-b2-probe/（临时，不入库），工作树未改任何 src/。
