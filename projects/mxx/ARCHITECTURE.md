# mxx — MeuOS C++ Compiler Architecture

> mxx = MeuOS CXX (C++) Compiler  
> **使命**：实现 C++11 核心子集，最终成为 Rust 编译器的后端

---

## 一、定位与飞轮角色

```
rustc (Rust 生态)
    │
    │  Rust 编译到 C++ IR
    │  (rustc_codegen_mxx)
    ▼
┌─────────────────────────────────────┐
│              mxx                     │
│  C++ Parser → Sema → IR → libmcc    │
│                                     │
│  输入: .cc/.cpp → libmcc.a(后端)     │
│  输出: .s 汇编                      │
└──────────┬──────────────────────────┘
           │
           ▼
    meuos-sysroot / meuos-kernel
```

mxx 在进化飞轮中的角色：

```
rustc 编译 Rust 项目 (通过 codegen_mxx)
    → 发现 C++ 符号缺失、ABI 不完整
        → mxx 补全 C++ 能力
            → mkit 补全符号表
                → MeuOS Kernel 实现 syscall
                    → 更多 Rust 软件能编译运行
                        → 循环往复
```

---

## 二、阶段路线图

### Phase 0：基础设施（当前）

| 任务 | 产出 | 依赖 |
|------|------|------|
| 创建 projects/mxx/ 骨架 | 目录结构 + Makefile | 无 |
| 软链接 mcc 公共头文件 | include/mcc.h, ir.h | mcc |
| 从 mcc 拷贝 lex/ 层 | scan.c, token.c, pp.c | mcc |
| 精简 main.c | mxx 入口，支持 --target | mcc |
| Hello World 编译 | `mxx test/hello.cc -o hello` | libmcc.a |

### Phase 1：C 子集兼容

| 任务 | 产出 | 依赖 |
|------|------|------|
| 从 mcc 拷贝 parse/ + sema/ | 完整 C 解析器 | Phase 0 |
| 从 mcc 拷贝 irgen/ | AST→IR 管线 | Phase 0 |
| 链接 libmcc.a 作为后端 | 复用优化/emit/target | mcc 构建 |
| C 文件编译验证 | `mxx test.c` 与 `mcc test.c` 一致 | Phase 1 |

### Phase 2：C++ 基础特性

| 特性 | 模块 | 复杂度 |
|------|------|--------|
| namespace | parse/ + sema/ | ⭐ |
| class + 成员函数 + this | parse/ + sema/ + irgen/ | ⭐⭐⭐ |
| 构造函数/析构函数 | sema/ + irgen/ | ⭐⭐ |
| 访问控制 (public/private) | sema/ | ⭐ |
| new/delete 表达式 | irgen/ | ⭐⭐ |
| 引用类型 & | sema/ + irgen/ | ⭐⭐ |

### Phase 3：C++11 核心

| 特性 | 模块 | 复杂度 |
|------|------|--------|
| auto / decltype | sema/ + irgen/ | ⭐⭐ |
| 函数模板 | parse/ + sema/ | ⭐⭐⭐⭐ |
| 类模板 | parse/ + sema/ | ⭐⭐⭐⭐ |
| 继承 + 虚函数 + 虚表 | sema/ + irgen/ | ⭐⭐⭐ |
| lambda 表达式 | parse/ + irgen/ | ⭐⭐⭐ |
| 重载决议 | sema/ | ⭐⭐⭐ |
| 异常处理 (try/catch/throw) | IR 扩展 + irgen/ | ⭐⭐⭐⭐⭐ |
| RTTI (typeid) | IR 扩展 + irgen/ | ⭐⭐⭐⭐ |

### Phase 4：Rust 后端就绪

| 任务 | 说明 |
|------|------|
| C++ ABI 稳定 | Itanium C++ ABI (x86_64), ARM64 ABI |
| 模板实例化完整 | 支持 rustc 生成的 C++ 绑定 |
| 虚表布局完整 | 支持 trait object 的 vtable |
| rustc_codegen_mxx | 让 rustc 可以用 mxx 作为后端 |
| Rust std 编译 | 编译 Rust 标准库 |
| Rust 项目回归 | 50+ Rust crate 编译验证 |

---

## 三、与 libmcc 的集成

```
FE_DIRS := src/driver src/lex src/parse src/sema src/irgen
BE_DIRS := src/ir src/opt src/abi src/emit src/target src/util
LIB := build/libmcc.a    (由 BE_DIRS 构建)
BIN = FE_OBJS + LIB      (由 mcc Makefile 构建)
```

mxx 的构建：

```
MCC_DIR = ../mcc
CFLAGS += -Iinclude -I$(MCC_DIR)/include
LDFLAGS += -L$(MCC_DIR)/build -lmcc

$(BIN): $(OBJS) $(MCC_DIR)/build/libmcc.a
    $(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) -lmsys
```

### 可复用的 mcc 模块

| mcc 模块 | mxx 复用方式 |
|----------|-------------|
| src/ir/ | 直接链接 libmcc.a |
| src/opt/ | 14 个优化 pass 不变 |
| src/abi/ | 直接链接 libmcc.a |
| src/emit/ | 直接链接 libmcc.a |
| src/target/*/ | 6 架构后端直接复用 |
| src/util/ | 直接链接 libmcc.a |
| src/lex/ | 拷贝后扩展 C++ token |
| src/parse/ | 拷贝后加 namespace/class/template |
| src/sema/ | 扩展引用类型、namespace、重载 |
| src/irgen/ | 扩展虚表、RTTI、异常 |

---

## 四、关键技术决策

### 4.1 解析器：从 C 出发迭代

从 mcc 的 C11 解析器开始，逐层叠加 C++ 特性：

```
C 解析器 (mcc parse/)
    → 加 namespace 作用域
        → 加 class 定义 + 成员
            → 加模板声明 + 实例化
                → 加重载决议
```

### 4.2 模板：惰性实例化

模板声明 → 解析并缓存 AST → 实例化时拷贝 AST + 替换类型 → 编译。
初期不实现两阶段查找，实例化时才解析模板体。

### 4.3 异常：初始不实现

`throw` 直接调用 `abort()`。异常需要 IR 扩展（`Othrow`/landingpad）
和 Itanium C++ ABI 运行时（`_Unwind_*`），留到 Phase 3 末期。

### 4.4 Rust 后端路径

```
Rust (rustc)
   ├─→ rustc_codegen_llvm  — 现有默认
   ├─→ rustc_codegen_gcc   — 社区项目 (gcc backend)  
   └─→ rustc_codegen_mxx   — MeuOS 目标 (mxx backend)
```

rustc_codegen_mxx 工作方式：
1. Rust 编译器将 MIR 翻译为 C++ 代码
2. mxx 编译 C++ 代码为汇编
3. 最终链接为 MeuOS 原生二进制

---

## 五、文件结构

```
projects/mxx/
├── ARCHITECTURE.md
├── Makefile
├── include/
│   └── mcc.h → ../mcc/include/mcc.h (软链接)
├── src/
│   ├── driver/main.c
│   ├── lex/{scan,token,pp}.c
│   ├── parse/{decl,specs,class,template,expr*,stmt}.c
│   ├── sema/{type,scope,overload,template}.c
│   └── irgen/{emit,expr,class}.c
└── test/
    ├── hello.cc
    └── class.cc
```

## 六、测试门禁

| Phase | 门禁 | 通过标准 |
|-------|------|----------|
| 0 | `mxx test/hello.cc` | exit=0 |
| 1 | `mxx test/*.c` vs `mcc` | 输出一致 |
| 2 | `mxx test/class.cc` | 类成员/构造/析构正确 |
| 3 | `mxx test/template.cc` | 模板实例化 + 虚函数调用 |
| 4 | Rust crate 编译 | `cargo build` 通过 |
