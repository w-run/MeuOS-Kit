# m++ C++20 冒烟基线（cpp20-smoke-baseline.md）

> 状态：冒烟基线记录（worker-cpp20，2026-08-03）。只测不改业务代码。
> 基线：HEAD 75b0853（worktree-mxx-work，含 Q✅f8f0044 / R✅93ab4b4 / V✅随93ab4b4），
> `m++ 0.1.0`（02:25 构建，与 HEAD 源码一致），`--specs=host`。
> 关联：缺口清单见 `cpp20-gaps.md`（6ab0386）；本文件是其在最新 HEAD 上的**回归复核 + 崩溃探测**。

---

## 0. 结论

- **无回归**：cpp20-gaps.md 记录的支持项与缺口，在最新 HEAD 上**逐项一致**，缺陷波修复（Q/R/V）未引入 C++20 侧回归。
- **Q/R 修复正向确认**：`delete nullptr` no-op、concept 形参名 ≠ `T`（`X`/`U`/`V`/`Y`/`Z`）均通过。
- **无新增 bug**：15 项组合/深度嵌套崩溃探测全部 PASS（concept 4 层链、consteval 递归、模板内 `<=>`、lambda 调 consteval 等）。
- **已知缺陷 U 复现并细化**：见 §3——两个独立面均确认，触发条件比原登记更精确（**前端缺陷，MIR=0/1 双路径同崩**）。

---

## 1. 已支持项复核（PASS）

| 项 | 探针 | 结果 |
|---|---|---|
| consteval 常量折叠 | `sq(6)==36` | PASS 运行 rc=0 |
| consteval 非常量实参降级 | `int v=7; sq(v)` | PASS（简化集行为，标准应报错，见 gaps §2.4） |
| consteval 递归 + 嵌套 | `fact(5)==120 && sq(fact(3))==36` | PASS |
| 三向比较 `<=>`（标量） | `a <=> b` | PASS |
| `<=>` 在模板体内 | `template<T> int cmp(T,T){return a<=>b;}` | PASS |
| `<=>` 在类成员函数内 | `int cmp(int o){return x<=>o;}` | PASS |
| concepts requires-clause | `requires Integral<T>` | PASS |
| concepts 形参名 ≠ T（缺陷 R） | `concept Four = sizeof(X)==4` | PASS（93ab4b4 修复确认） |
| concepts 组合 `&&` | `requires Four<Z> && NotVoid<Z>` | PASS |
| concepts 4 层链 + `\|\|` + `!` | C1→C2→C3→C4 | PASS |
| consteval + concept 组合 | `requires Int4<T>` 体内调 consteval | PASS |
| lambda 体内调 consteval | `[](){ return sq(4); }` | PASS |
| 指定初始化器 | `P p = {.x=3, .y=4}` | PASS |
| delete/delete[] nullptr（缺陷 Q） | `B *p=0; delete p; delete[] p;` | PASS（f8f0044 修复确认） |

---

## 2. 缺口复核（与 cpp20-gaps.md 一致，无变化）

| 缺口 | 报错（最新 HEAD 实测） |
|---|---|
| range-for（类型化） | `expected ',' or ';' after declarator, saw ':'` |
| range-for（auto） | `'auto' variable requires an initializer` |
| range-for 初始化语句（C++20） | `expected primary expression` |
| requires 表达式 | `expected declaration or function definition` |
| 非类型模板参数 NTTP | `expected 'typename' or 'class' in template parameter list` |
| 类类型 `operator<=>`（成员） | `unsupported operator for overloading` |
| 类类型 `operator<=>`（defaulted） | `unsupported operator for overloading` |
| constinit | `expected declaration or function definition` |
| explicit(bool) | `no type in struct member declaration` |
| char8_t | `declaration has no type specifier` |
| using enum | `undeclared identifier: using` |
| lambda 模板参数 `[]<typename T>` | `expected lambda body` |
| constexpr lambda | `expected lambda body` |
| 括号聚合初始化 `P p(3,4)` | `no matching constructor for object 'p'` |

> 复核范围说明：range-for / requires 表达式 / NTTP 属语法解析层，与 worker-lambda 在途的
> lambda 捕获降级（缺陷 S/T）区域不重叠，可安全实现；本轮只记录不动代码。

---

## 3. 缺陷 U 细化（size-0 类，前端缺陷）

原登记为"size-0 空类传参/返回编译崩溃"。本轮探测**确认复现**并给出更精确的触发面：

### 3.1 两个独立面

| 面 | 最小复现 | 症状 |
|---|---|---|
| **A. 按值传参** | `class Empty{}; int pass(Empty e){return 1;} Empty e; pass(e);` | 编译 rc=0，**运行时 SIGSEGV（rc=139）** |
| **B. 按值返回后被调用** | `class Empty{}; Empty make(){Empty e; return e;} make();` | **编译期 SIGSEGV（rc=139）**，`-S` 亦崩，产出 0 字节 |

### 3.2 触发边界（新增结论）

- **判定标准是"无实例数据成员"**，不是字面空类：
  - `class Empty {}` → 触发
  - `class FuncOnly { int f(int); }`（只有成员函数）→ 触发
  - `class StaticOnly { static int s; }`（只有静态成员，不占布局）→ **触发**（运行 rc=139）
  - `class OneByte { char c; }` / `class OneInt { int m; }` → **正常**（对照组 PASS）
- **面 B 的崩溃点是「调用」而非「定义」**（关键定位）：
  - `Empty make();`（仅声明）→ 编译 rc=0
  - `Empty make(){Empty e; return e;}`（定义但不调用）→ 编译 rc=0
  - `make();` / `Empty x = make();`（调用）→ **崩溃 rc=139**
- **属前端缺陷，非 MIR 后端**：`MCC_USE_MIR=0` 与 `=1` 双路径均 rc=139。
- **C 侧不受影响**：mcc 对空 struct 直接报错 rc=1（C 不允许空 struct），是正常诊断。
- 另注：`Empty make(){ return Empty(); }`（临时对象返回）编译 rc=1 报错，非崩溃。

### 3.3 修复建议方向

崩溃在「调用返回 size-0 聚合的函数」的降级路径。建议查：
- 返回值 ABI 分类（size==0 时聚合返回槽/隐藏指针计算）
- 参数传递降级对 size-0 类型的拷贝路径（面 A）
两面很可能同源——size-0 聚合在 ABI 层缺少专门分支，落入按大小计算的通用路径后除零/空指针。

---

## 附录：探针位置

`/tmp/cpp20smoke/`：s1~s4（已支持基线）、g1~g15（缺口）、c1~c10（崩溃探测组合）、
cU_*/u1~u10（缺陷 U 最小化与边界）。复跑：`m++ --specs=host -o out X.cc && ./out`。

> 注意：判定编译器崩溃时不要用 `cmd 2>&1 | head` —— 管道会把退出码换成 `head` 的，
> 掩盖 rc=139。本轮初测曾因此误判缺陷 U 面 B 为"已修复"。
