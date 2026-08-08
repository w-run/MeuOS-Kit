# m++ C++23 完整套件规划 — 2026-08-08

> 状态：侦察规划文档，**不改代码**。由 loong-worker 按 HEAD 40b9b86b 实测验证。
> 结论：HEAD 已修复 cpp23-gaps.md 中多项缺口（D1/D4/noexcept/extern C/constinit/继承构造/ref array/using enum），
> **当前 m++ 尚未实现的 C++23 全线剩余缺口估算约 12 项，总工作量 4~6 人·周**（不含 coroutines/modules）。

---

## 0. 变更速报（相对 cpp23-gaps.md / cpp20-gaps.md）

| 缺口 | cpp23-gaps 状态 | 当前 HEAD (40b9b86b) | 说明 |
|:-----|:---------------|:--------------------|:-----|
| D1 `const T&` 运算符 | 🔴 待修 | ✅ **已修** | free/member `const T&` 形参均可跑 |
| D4 非空类返回 | 🔴 待修 | ✅ **已修** | `V make(){V v; v.n=7; return v;}` exit=0 |
| noexcept | 🔴 基线缺口 | ✅ **已修** | `int f() noexcept {}`、成员 noexcept 均通过 |
| `extern "C"` | 🔴 基线缺口 | ✅ **已修** | 链接说明被接受，调用正常 |
| `constinit` | 🔴 缺关键字 | ✅ **已修** | 关键字已 lex，可编译运行 |
| 继承构造 `using B::B` | 🔴 基线缺口 | ✅ **已修** | 编译+链接通过 |
| 引用数组参数 `int(&a)[3]` | 🔴 基线缺口 | ✅ **已修** | 编译+运行通过 |
| `using enum` | 🔴 缺口 | ✅ **已修** | 编译通过 |
| `explicit(bool)` | ⛔ 缺口 | ✅ **已修** | 语法接受（语义未确认） |

---

## 1. 剩余缺口分级清单（实测）

### P0 — 高阻塞，广泛影响

| # | 特性 | 现象 | 根因/位置 | 影响 | 难度 |
|:-:|:-----|:-----|:----------|:-----|:----:|
| 1 | **虚继承** | `struct B : virtual A` → 「'virtual' 不是类类型」 | `cpp_parse.c` base-spec 解析只认 `public/protected`，`virtual` 关键字未识别。**零代码存在**：无 vbptr/vbtable/DAG 布局/共享初始化。钻石型 `D : B, C : virtual A` 完全不可用 | C++98 关键特性，阻塞 std::iostream/异常体系 | **高**（全新模块） |
| 2 | **惰性实例化（D2）** | 未 ODR-used 成员函数体被急切实例化，体内错误误报 | `cpp_template_decl` / 类模板实例化路径 | 模板健壮性，阻塞 STL 类模板通用性 | **中** |

### P1 — 中等阻塞

| # | 特性 | 现象 | 根因/位置 | 影响 | 难度 |
|:-:|:-----|:-----|:----------|:-----|:----:|
| 3 | **lambda mutable** | `[x]() mutable {...}` → 「expected lambda body」 | `cpp_lambda_expr` 参数列表后未识别 `mutable` 关键字。也阻塞 `noexcept`/`constexpr` lambda 限定符 | C++11 基线（P1102 C++23 子集前驱） | **低-中** |
| 4 | **lambda 模板参数** | `[]<typename T>(T x){}` → 「expected lambda body」 | lambda 头只解析 `[cap](params)`，无 `<...>` 模板参数解析 | C++20 特性，泛型 lambda 进阶 | **中** |
| 5 | **static operator()** (P1169) | `struct F { static int operator()(int x){} }; f(42)` → 「no matching member for 'operator_cl'」 | `cpp_parse.c` 运算符重载注册未处理 static 限定；`cpp_expr_op.c` 调用点未区分 static | C++23 部分支持（语法接受但 static 忽略） | **中** |
| 6 | **char8_t** | `const char8_t* s = u8"..."` → 类型不兼容 | 无 `char8_t` 关键字/类型；u8 字面量当前为 `char` 数组 | C++20 基线 | **中** |
| 7 | **constexpr lambda** | `constexpr auto f = [](int x){};` → 「requires a constant expression initializer」 | 闭包类型不能作为 constexpr 变量初值；lambda `operator()` 未标记 constexpr | C++17 特性 | **中高** |
| 8 | **consteval 即时调用强制** | 非常量实参 `sq(v)` 静默降级运行时（标准须报错） | `cpp_constexpr_eval` 调用点未做 is-constant 检查 | 行为正确性 | **低-中** |
| 9 | **类类型三向比较** | `auto operator<=>(const S&) const = default` → 「unsupported operator」 | `cpp_op_mangle` 无 `<=>` 分支；defaulted 比较缺失 | C++20 特性 | **中高** |

### P2 — 低阻塞

| # | 特性 | 现象 | 根因/位置 | 难度 |
|:-:|:-----|:-----|:----------|:----:|
| 10 | **constexpr 引用/数组/类对象成员** | `const int& r = v` → 非常量初值；constexpr 局部类对象 `s.n=5` → 非整型常量 | `cpp_constexpr_eval` 无 mini 内存模型 | **高** |
| 11 | **概念简写语法** | `template<Concept T>` → 只认 typename/class；`f(Concept auto)` → 语法错误 | `cpp_parse.c:3713` 模板参数循环硬化 | **低-中** |
| 12 | **requires 表达式合入** | 已由 worker-req4 在途实现，待合入 HEAD | cpp_parse.c 概念体缓冲；四类表达式语法 | **中（在途）** |
| 13 | **类模板 + requires-clause** | `template<typename T> requires C<T> struct S {};` → 顺序 bug | `cpp_parse.c:3759` is_class 检测早于 requires 消费 | **低** |
| 14 | **`[[nodiscard]]` 语义** | 语法已接受；`[[nodiscard]]` 返回值丢弃警告 da7a107 | attr.c 属性表；其余属性无语义 | **低-中** |
| 15 | **`explicit(bool)` 语义** | 语法接受；`explicit(true)` 如同 `explicit`，`explicit(false)` 应等同无 explicit | cpp_parse.c init-ctor 路径；目前 explicit 二值行为仅 static_assert(false) 可体现 | **低** |
| 16 | **`[[no_unique_address]]`** | 语法接受，布局未按属性压缩 | 语义忽略，属性表无处理 | **中** |
| 17 | **constexpr 成员函数在常量对象上调用** | `constexpr C c; c.sq()` → 「no matching member」 | 常量对象+constexpr 成员未接通 | **中** |
| 18 | **重写相等候选 (P2468)** | `a == 5` 应反转候选 | 重载决议无反向候选生成 | **中高** |
| 19 | **mutable 捕获** | `[&x]` → 「not supported yet」 | cpp_lambda 捕获路径标记 | **低（已知）** |
| 20 | **init-capture** | `[n=42]` → 「cannot capture variable」 | 同引用捕获路径 | **低-中** |

---

## 2. 大缺口工作量评估

### 2.1 虚继承（P0 最高阻塞）

**现状**：
- `struct D : B, C : virtual A`（菱形继承）— `virtual` 关键字在 base-spec 解析报错，零实现
- 现有 vtable 系统（cpp_vtable.c）只处理单继承和多继承（非虚），无 **vbptr/vbtable** 概念
- `cpp_base_offset()` 递归按非虚布局计算偏移，无 DAG 共享检测

**实施范围**：

| 模块 | 现有代码 | 需新增 |
|:-----|:---------|:-------|
| `type` 结构 (`mcc.h`) | `structunion.poly` / `primary_base` | 需加 `has_virtual_base` / `virtual_bases` 链 |
| `cpp_parse.c` base-spec 解析 | 只解析 `public/protected`，忽略 `virtual` | 读 `virtual` token → 标记基类为 virtual |
| `cpp_vtable.c` 布局 | 线性（主基类→次基类→自身） | **DAG 计算**：共享虚基类子对象偏移（vs VBASE reloc），多继承下每个虚基类有独立 vbtable 入口 |
| `cpp_vtable.c` 构造发射 | `cpp_emit_vtables` 只发射 vptr 赋值 | **vbptr 初始化**：构造函数体须在每个虚基类子对象偏移处插入 vbptr 赋值（或 `__vtt` 构造虚表） |
| `cpp_vtable.c` vcall | `cpp_make_vcall` vptr->slot 间接跳 | 虚基类偏移修正：`this` 必须先调至虚基类子对象，再加载 vptr |
| `cpp_method.c` / `cpp_newdel_expr.c` | 成员调用 this 指针 | 虚基类 this 调整（thunk 或偏移） |
| `cpp_mangle.c` 名字修饰 | 已支持 Zv 编码？需确认 | 虚继承方法需加 vcall-thunk thunk |

**关键决策**：
- **ABI 选择**：m++ 是否遵循 Itanium C++ ABI（vbptr/vbtable/vtt）或自研简化版？
  - 推荐自研**简化版**（m++ 非 ABI 兼容目标，自举工具链不需跨编译器 LINK）：虚基类子对象用固定偏移 `base_offset` + 独立 vtable 组，无 vtt 构造表，构造函数对每个虚基类子对象直接赋值 vptr（非数组式批量）
  - 例外：`primary_base` 始终是第一个非虚多态基类；虚基类从不自为主基类

**工作量估算**：~2 人·周
- base-spec 解析 + virtual token：1 天
- type 结构扩展 + DAG 偏移计算：3 天
- vtable 发射（vbptr 赋值）：2 天
- vcall this 调整 + 构造函数链：2 天
- 测试 + 调试：2 天

### 2.2 惰性实例化（P0 D2）

**现状**：
- `cpp_template_decl` 记录模板 token 序列。类模板实例化路径（`cpp_class_tmpl_instantiate`）在实例化时对**所有**成员函数都调用 `cpp_template_instantiate`（急切实例化）
- 标准要求：类模板特化实例化时，**只有虚函数和友元函数被实例化**，普通成员函数只在 ODR-used 时实例化

**实施范围**：
| 模块 | 现有代码 | 需改动 |
|:-----|:---------|:-------|
| `cpp_parse.c` 类模板实例化 | 实例化全部成员函数 | 改为只缓冲 token（注册但不实例化），ODR-used 时按需实例化 |
| `cpp_template_decl` / `cpp.h` | 无「延迟实例化注册」接口 | 加 `cpp_template_lazy_instantiate` / 延迟列表 |

**工作量估算**：~0.5 人·周

### 2.3 lambda 增强（mutable/模板参数）

**现状**：
- `cpp_lambda_expr` 解析 `[cap](params) -> ret { body }`，三个限定符缺失

**实施**：
- `mutable` token → lexer 已有 CPP_TMUTABLE？确认后解析器加 `mutable` 标记
- `noexcept` token 已识别的成员函数中已通，lambda 未加
- `<typename T>` 模板参数 → lambda 头 `[` 后加 `template<...>` 解析

**工作量估算**：2 天（mutable + noexcept）+ 3 天（模板参数）= ~0.5 人·周

### 2.4 char8_t（P1）

**实施**：
- `mcc.h` 加 `TYPECHAR8` / `uchar8_t` 等价于 `unsigned char` 或真类型
- `cpp_scan.c` / `cpp_tokens.h` 加 `CPP_TCHAR8_T` 关键字
- `cpp_tokens.h` 定义 `char8_t` token
- u8 字面量类型化为 `const char8_t[]`（当前为 `const char[]`）

**工作量估算**：~3 天

### 2.5 constexpr lambda（P1）

**实施**：
- 在 lambda `operator()` 合成处标记 `constexpr`（体无运行时副作用时）
- `cpp_constexpr_eval` 增加 lambda closure 调用路径：`operator()(args)` 解释
- 按值捕获的闭包成员在 constexpr 上下文中为 constexpr 变量

**工作量估算**：~4 天（难度中高，需理解 closure 在 constexpr 外的 lowering）

---

## 3. 推荐实施路线（按依赖/收益）

```
Phase 0 — 地基修复（建议立即启动）
  ├── 虚继承（2 人·周）        ← 最高阻塞，解锁菱形继承
  └── 惰性实例化 D2（0.5 人·周）  ← 模板健壮性

Phase 1 — C++20 收官
  ├── lambda mutable/noexcept（2 天）
  ├── char8_t（3 天）
  ├── 类类型三向比较（4 天）      ← <=> 重载 + defaulted + 重写
  ├── consteval 即时调用强制（1 天）
  ├── constexpr lambda（4 天）
  ├── lambda 模板参数（3 天）    ← 可先于 constexpr lambda
  └── 概念简写语法 + requires 合入 + 类模板 requires bug（2 天）

Phase 2 — C++23 深化
  ├── static operator() P1169（2 天）
  ├── constexpr 引用/数组/类对象成员（4 天）
  ├── [[nodiscard]] 语义深化（2 天）
  ├── [[no_unique_address]] 语义（2 天）
  ├── 重写相等候选 P2468（3 天）
  ├── constexpr 成员函数常量对象（2 天）
  └── 引用/init-capture（2 天）
```

**总计工作量估计**：~4 人·周（不含 Phase 2 中可选深项目如 mini 内存模型）

---

## 4. 关键代码位置索引

| 缺口 | 文件:行 | 说明 |
|:-----|:--------|:-----|
| 虚继承 base-spec | `src/cpp/parse/cpp_parse.c` 基类列表解析（当前忽略 `virtual`） | 解析 `:` 后的 `[virtual] [public|protected]` |
| 虚继承 vtable | `src/cpp/parse/cpp_vtable.c` (全部) | 需重写 DAG 布局 |
| 虚继承 this 调整 | `src/cpp/parse/cpp_method.c` | 成员调用时虚基类偏移 |
| 惰性实例化 D2 | `src/cpp/parse/cpp_parse.c` 类模板实例化路径 | 改为缓冲 token 而非立即实例化 |
| lambda 解析 | `src/cpp/parse/cpp_parse.c` `cpp_lambda_expr` | 约 100 行，在 primaryexpr [`] 入口 |
| 运算符 mangle | `src/cpp/parse/cpp_parse.c:1162-1179` (`cpp_op_mangle`) | 无 TSPACESHIP 分支 |
| concepts 简写 | `src/cpp/parse/cpp_parse.c:3713` 模板参数循环 | 硬化 `typename`/`class` |
| 类模板 requires bug | `src/cpp/parse/cpp_parse.c:3759 vs 3822` | is_class 检测早于 requires 消费 |
| constexpr 求值器 | `src/cpp/parse/cpp_constexpr.c` + `cpp_constexpr_eval.c` + `cpp_constexpr_ctrl.c` + `cpp_constexpr_agg.c` | DAG 主文件 |
| 属性语义 | `src/c/parse/attr.c` | ATTR 表只认 C 风格 |

---

## 5. 参考源

| 特性 | 参考源（实际存在） |
|:-----|:------------------|
| 虚继承 | `reference/aburiscript/collect/collect_finalize_class.cpp`（vbase 偏移计算、vtt 构造） |
| 虚继承 ABI | `reference/aburiscript/abi/mangle.cpp`（Zv/Zt 编码） |
| 惰性实例化 | `reference/aburiscript/collect/collect_templates.cpp`（模板实例化点 vs ODR-used 追踪） |
| char8_t | `reference/aburiscript/parser/parser_lexer.cpp`（`char8_t` 关键字） |
| lambda 模板参数 | `reference/aburiscript/parser/parser_expr.cpp:1771` lambda 表达式（完整语法） |
| constexpr lambda | `reference/aburiscript/constexpr/consteval_engine.cpp` + `collect/collect_expr.cpp` |
| 属性语义 | `reference/aburiscript/ast/attributes.cpp`（属性 AST 表示/检查） |
| 重写候选 P2468 | `reference/aburiscript/collect/collect_overload.cpp`（重载候选排序/反向候选生成） |