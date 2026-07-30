# mxx — MeuOS C++ Compiler Architecture

> mxx = MeuOS CXX (C++) Compiler
> **使命**：从 C++98 → C++23 全谱系支持，最终成为 Rust 编译器的后端

---

## 一、定位与飞轮角色

```
rustc (Rust 生态)
    │  rustc_codegen_mxx (MIR → C++)
    ▼
┌─────────────────────────────────────┐  ref: aburiscript (193K 行 C/C++ 前端)
│              mxx                     │  ref: cxx-frontend (C++23 解析库)
│  Lex → Parse → Sema → IR → libmcc  │  ref: clank (Clang 包装)
│  输入: .cc/.cpp → 输出: .s          │
└──────────┬──────────────────────────┘
           ▼
    meuos-sysroot / meuos-kernel
```

mxx 在进化飞轮中：rustc 编译 → 发现 C++ 符号缺失 → mxx 补全 → mkit 补全 → Kernel 实现 syscall

---

## 二、阶段路线图（全谱系：C++98 → C++23 → Rust 后端）

### Phase 0：基础设施（当前）
- projects/mxx/ 骨架 + 软链接 mcc 头文件 + Makefile
- 从 mcc 拷贝 lex/（scan.c, token.c, pp.c）+ 精简 main.c
- 链接 libmcc.a
- **门禁**：`mxx test/hello.cc -o hello` exit=0

### Phase 1：C 子集兼容（从 mcc 拷贝）
- 拷贝 mcc 的 parse/ + sema/ + irgen/
- C 文件编译与 mcc 一致

### Phase 2：C++98（基础特性）
- namespace 作用域 + using
- class + 成员函数 + this 指针
- 构造/析构函数、初始化列表
- new/delete 表达式
- 引用类型（&）
- 访问控制（public/protected/private）
- 继承（单继承）
- 虚函数 + 虚表（vtable）
- 函数重载决议
- 运算符重载
- 模板基础（函数模板、类模板）

### Phase 3：C++11（现代 C++ 起点）
- auto / decltype
- 右值引用 && / 移动语义
- lambda 表达式
- 模板进阶（变参模板、模板别名）
- 继承进阶（虚继承）
- RTTI（typeid + dynamic_cast）
- 异常处理（try/catch/throw → Itanium ABI）
- constexpr（基础函数）
- nullptr / override / final
- 强类型枚举（enum class）

### Phase 4：C++14 / C++17
- 泛型 lambda（C++14 auto 参数）
- constexpr 扩展（C++14/17）
- if constexpr（编译期条件）
- 结构化绑定（C++17）
- 折叠表达式（C++17）
- inline变量（C++17）
- std::variant / std::optional 支持
- 文件系统库编译

### Phase 5：C++20 / C++23
- 模块（C++20 modules）
- 概念（C++20 concepts）
- 协程（C++20 coroutines）
- 编译期计算增强
- std::format 编译支持
- C++23 标准库特性编译
- mxx 自举（用 mxx 编译 mxx）

### Phase 6：Rust 后端
- Itanium C++ ABI（mangle + 异常展开）
- rustc_codegen_mxx 原型
- Rust hello world 通过 mxx 编译
- Rust std 编译验证

---

## 三、技术策略（从参考项目汲取）

### 3.1 解析器：递归下降 + tentative parsing

aburiscript 使用试探性解析（`tentative_syntax_probe.h`）处理 C++ 的声明/表达式歧义：

```cpp
// 例如 "T(x)" 可能是：
//   - 函数风格类型转换（T x）
//   - 变量声明（T x; 省略了分号的上下文）
// 解析器先试探 parse as declaration，失败再回退到 expression
```

mxx 将从 mcc 的 C 解析器继承递归下降风格，叠加 C++ 歧义处理。

### 3.2 模板：惰性实例化，不实现两阶段查找

参考 aburiscript 的模板架构：

```
collect_templates_*.cpp (10+ files):
├── declaration / specialization
├── argument deduction
├── substitution / materialization
├── deferred instantiation
├── packs (variadic templates)
└── depth / resolution
```

mxx 第一阶段只做最基本的模板：声明 → 缓存 AST → 实例化时替换类型 → 编译。
Phase 3 逐步加参数推导和特化。

### 3.3 AST 节点设计

参考 aburiscript 的 StmtKind/DeclKind 层次（15 个 enum class）和 cplusplus 的 ast_kind.h：

```
mxx 使用 C 风格 struct + enum（与 mcc/mcc.h 一致）：
enum StmtKind { ... };
enum DeclKind { ... };
struct Stmt { enum StmtKind kind; };
struct Decl { enum DeclKind kind; };
```

C++ 特有的节点（CXXRecordDecl、TemplateDecl 等）逐步加入。

### 3.4 错误恢复

C++ 解析器需要健壮的错误恢复。aburiscript 在 `parser_tentative.cpp` 中实现了试探性同步：
```
错误 → 跳过 token 直到找到同步点（; {} #endif）
```

mxx 初期不做复杂恢复，出错即报。

### 3.5 C++ ABI

最终目标：Itanium C++ ABI（x86_64）。需要：
- 名称修饰（mangle）
- 虚表布局
- RTTI 结构
- 异常处理帧

参考 aburiscript 的 `abi/mangle.cpp`。

---

## 四、与 libmcc 的集成

```
MCC_DIR = ../mcc
CFLAGS += -Iinclude -I$(MCC_DIR)/include
LDFLAGS += -L$(MCC_DIR)/build -lmcc -lmsys
```

复用模块：ir, opt, abi, emit, target, util（全部通过 libmcc.a）

---

## 五、文件结构

```
projects/mxx/
├── ARCHITECTURE.md   ← 本文件
├── Makefile           ← 构建框架
├── include/
│   └── mcc.h          → ../mcc/include/mcc.h (软链接)
├── src/
│   ├── driver/main.c  ← 入口
│   ├── lex/{scan,token,pp}.c   ← 从 mcc 拷贝
│   ├── parse/{decl,specs,expr,stmt,class,template}.c
│   ├── sema/{type,scope,overload,template}.c
│   └── irgen/{emit,expr,class}.c
├── test/hello.cc
└── ref/              ← 参考项目
    ├── aburiscript/   (193K 行 C/C++ 前端)
    ├── cplusplus/     (C++23 解析器)
    └── clank/         (Clang 前端)
```
