# 实现策略参考（.agents/reference/strategy.md）

> 从 AGENTS.md §6/§7 下放（2026-08-04）。实现策略与任务编排方法论。

## 6. 实现策略

### 6.1 实现路径与特化原则

**先标准化可用，再增强优化。** 所有组件遵循三阶段实现路径：

1. **标准化可用**：实现 ISO/POSIX 标准定义的接口，确保正确性和完整兼容性。MeuOS 的目标是真实可用的成熟发行版，对外的标准兼容性不妥协、不阉割。
2. **有利特性**：在标准兼容基础上，增加 MeuOS Next 实际需要的便利特性（便捷命令行选项、扩展 API 等），作为标准接口的补充而非替代。
3. **性能优化**：在功能和接口稳定后，针对 MeuOS 的实际工作负载做性能优化。

**组件间特化**：Kit 的所有组件都是自己实现的，架构上相互知晓彼此的内部逻辑。组件之间不必通过通用兼容性接口通信，而是可以利用内部知识做特化实现：

- **mcc -> mt/as**：mcc 知道 mt/as 支持的编码子集和偏好，直接生成最优汇编，无需覆盖 gas 全语法
- **meow -> mcc**：meow 知道 mcc 的编译速度和依赖特性，可以做针对性的并行调度和增量构建
- **libc -> mcc**：libc 知道 mcc 的 ABI 约定和优化能力，可以直接配合编写高效实现
- **mt/ld -> libc**：ld 知道 libc 的段布局和符号约定，可以做针对性的链接优化
- **msh -> meow**：shell 可以直接调用 meow 的内部 API 而非走命令行

这种特化只影响 Kit 组件之间的内部协作路径，不影响对外标准兼容性。外部软件通过标准接口使用 Kit 工具，行为与 GNU 对应物一致。

### 6.2 参考资源（节省算力）

**核心原则：优先参考成熟社区实现，避免从零推导繁琐算法而浪费算力。**

#### 本仓库已提供的只读参考树（`reference/`，gitignored，勿改勿提交）

| 路径                        | 用途                                                                          |
| --------------------------- | ----------------------------------------------------------------------------- |
| `reference/cproc/`        | mcc 前端设计参考（词法/语法/语义/类型系统）                                   |
| `reference/qbe/`          | mcc 后端设计参考（IR/指令选择/寄存器分配/各 arch emit）                       |
| `reference/musl/`         | meuos-libc 算法参考（mallocng/stdio/pthread/...）                             |
| `reference/tinycc/`       | 轻量 C 编译器参考（快速编译、简单后端、tcc 的 preprocessor）                  |
| `reference/cxx-frontend/` | m++ C++ 前端参考（C++23 词法/语法/语义解析、AST 设计）                        |
| `reference/aburi/`        | m++ C++ 前端参考（lexer/parser/preprocessor/ast/constexpr、Itanium ABI 降级） |

#### 鼓励参考的其他社区资源

- **libc 算法**：musl（首选，已 vendored）、Cosmopolitan Libc、serenityOS LibC、PDCLib
- **编译器设计（C）**：cproc/QBE（已 vendored）、chibicc、9cc、lacc、cparser
- **编译器设计（C++）**：cxx-frontend（C++23 完整前端，已 vendored）、aburi（C/C++ 前端全流程，已 vendored）
- **构建系统**：redo、tup、ninja、bear-make
- **工具集**：Rust uutils、BusyBox、serenityOS Utilities
- **Shell**：dash（POSIX sh 参考）、serenityOS Shell
- **构建工具**：GNU m4/Bison/Flex/Gperf（参考行为和语法兼容性，不复制源码）
- **通用知识库**：OSDev Wiki、Linux man-pages、各 arch 的 ELF/ABI spec

#### 边界（与 §4 禁止事项一致）

- **参考算法与结构，但用本项目自己的代码重新实现**；不直接复制任何参考源码。
- 仍受 §4 约束：核心库与 mcc 源码中**禁止** glibc 专有符号、LLVM/Clang、GCC 代码。
- 所有实现必须能被 mcc 自身编译（自举验证），并过对应 `make check`。

**一句话**：站在巨人肩膀上--读参考实现理解算法，再用自己的手写出来，不要把算力花在重新发明轮子上。

---

## 7. 任务编排策略

> 本条是一般性方法论，适用于本项目所有复杂任务的规划与执行。
> 编译器特性实现、libc 函数实现、工具链开发等任何需要多步骤完成的任务，都应遵循此策略。

### 7.1 任务颗粒度原则

任务必须拆到足够细，使得**低推理度模型也能独立完成**。

验收标准：

- 每个任务只做**一件事**：一个文件 / 一处修改 / 一个函数的实现
- 单任务上下文在 200 行以内，避免膨胀
- 禁止一个任务跨越多个不相关的文件或模块

反例（禁止）："实现 riscv64 的 7 个运行时文件"
正例："创建 `src/arch/riscv64/atomic.S`（380 行，25 个原子函数）"

### 7.2 任务卡片五要素

每个任务必须包含以下五项，缺一不可：

| 要素                  | 说明                                                | 示例                                                                                         |
| --------------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| **1. 任务 ID** | 单词-短横线-代号。机器可读、语义自明；不用纯数字或字母数字编号 | `bug-riscv64-emit`、`ld-shared`、`meow-template-subst` |
| **2. 任务范围** | 精确的文件路径和修改内容                            | 创建`src/arch/riscv64/atomic.S`，实现 `__atomic_*` 系列                                  |
| **3. 参考来源** | 本仓库已验证实现（最优先）+ 社区标准实现 + 规范文档 | `src/arch/aarch64/atomic.S`（本仓库模板）+ `musl arch/riscv64/atomic_arch.h`（社区参考） |
| **4. 验收标准** | 可写成 shell 单行断言的检查项                       | `riscv64-linux-gnu-gcc -c atomic.S` 通过 && `nm atomic.o`                                   |
| **5. 依赖关系** | 仅依赖已完成的**前置**任务，线性单向无回溯    | 依赖 `riscv64-syscall` 已完成                                                              |

#### 任务 ID 命名规则

```
<组件>-<功能>-<修饰>     # 全部小写，短横线分隔
```

**组件前缀**（来源自明）：
- `bug-*`        — 阻塞性 bug（如 `bug-riscv64-emit`、`bug-i386-tls`）
- `mcc-*`        — 编译器相关
- `libc-*`       — C 库相关
- `ld-*`         — 链接器相关（mt/ld）
- `as-*`         — 汇编器相关（mt/as）
- `arch-*`       — 跨架构适配
- `c23-*`        — C23 标准特性
- `target-*`     — 架构 Target/子架构
- `specs-*`      — 编译参数/配置
- `triple-*`     — 三元组/ABI 相关
- `ci-*`         — CI/CD/测试基础设施

**命名原则**：
- 一眼能看出是哪个组件 + 做什么
- 避免纯数字（agent 记不住 `task-42` 是什么意思）
- 有层次：`mcc-pic-verify`（mcc 的 PIC 验证）好于 `verify-pic`
- 文件名/函数名风格：`ld-shared`、`meow-wildcard`、`libc-math`

### 7.3 线性单向任务流

- 将任务 DAG **拉平成线性阶段**，每个阶段内无交叉依赖
- 阶段内互不依赖的独立任务可**并行分派**给子 agent（使用 Agent 工具 `run_in_background: true`）
- 每个阶段完成后**立即验证**，失败不回退、不被后续任务污染
- **禁止回溯**：不允许 `ld-so` 完成后发现 `ld-shared` 有问题再回去改

### 7.4 并行开发

识别同一阶段内互不依赖的并行任务窗口：

```
Phase A: riscv64-syscall（基础，必须串行先做）
  → riscv64-atomic, riscv64-setjmp, riscv64-sigreturn（互不依赖，可并行）
  → riscv64-thread-clone（依赖前 3 个全部完成，串行收尾）
```

并行分派要点：

- 每个并行任务卡片**自包含**（含参考路径 + 验收命令），不依赖外部上下文
- 使用 `Agent` 工具发起时设置 `run_in_background: true`，确保子 agent 独立运行
- 并行任务完成后，**主 agent** 使用 `TaskOutput` 收集结果，统一验收和集成
- 并行数量 ≤ 4 个，避免上下文过大
- 子 agent 可自主进行多步操作（读文件、修改、验证）无需主 agent 干预

### 7.5 参考来源收集（算力节约的核心）

**在开始编码前**，先用 Explore 子 agent（`subagent_type: "Explore"`）收集：

1. **本仓库已验证的同类实现**（最优先）：同项目其他架构的对应文件，直接对照转录
2. **社区标准实现**：musl 对应目录、Linux 内核 UAPI 头文件
3. **规范文档**：ELF ABI spec 对应章节、syscall 编号表、指令集手册

关键是制作**架构差异对照表**，一次性消除重复推导：

| 功能               | 参考架构 (aarch64) | 目标架构 (riscv64) | 目标架构 (loongarch64) |
| ------------------ | ------------------ | ------------------ | ---------------------- |
| syscall 指令       | svc#0              | ecall              | syscall 0              |
| syscall 号寄存器   | x8                 | a7                 | $a7                    |
| 线程指针           | tpidr_el0          | tp                 | $tp                    |
| 原子 load-reserved | ldaxr              | lr.w/lr.d          | ll.w/ll.d              |
| TLS 变体           | variant I (GAP=16) | variant I (GAP=0)  | variant I (GAP=0)      |

对照表一旦建立，所有并行 agent 共享，避免各自重复查询。

### 7.6 阶段归档

每个阶段完成后**必须归档**，然后才能进入下一阶段。归档是提交的前置条件：

1. **运行 `make check`**（必须通过）。跨架构变更还需运行对应 `check-<arch>-bootstrap` 或 `check-<arch>-runtime`。如有回归**必须修复后才能提交**。
2. **更新 `.issues/` 文件**，将完成项标记为 `[x]`。
3. **更新 `ARCHITECTURE.md` / `PORTING.md`** 中的状态表和路线图。
4. **Git 提交**，提交信息格式：`<组件>: <阶段描述>（<文件清单>）`
   - 示例：`meuos-libc: riscv64 runtime 完成 (crt1/syscall/atomic/setjmp/sigreturn/thread_clone/tls)`
5. **合并到 `main`**（如果在工作分支上开发），合并后删除工作分支。
6. **禁止未通过 `make check` 就提交**，**禁止未归档就进入下一阶段**——归档是阶段完成的唯一定义。

### 7.7 完整执行模板

```
Phase 0: 环境确认（1 步，Explore 子 agent）
  → 验证参考文件存在、工具链就绪

Phase N: 主体实现（M 步，按依赖 DAG 串行+并行）
  → 基础文件（串行）
  → 独立文件（并行，Agent tool run_in_background:true）
  → 集成收尾（串行，TaskOutput 收集结果）
  → 每步验证 → 阶段归档

Phase N+1: 下一阶段（复用上一阶段的框架和对照表）
  → 适配差异项 → 验证 → 归档

Phase Final: 公共层清理 + 生成报告
```

### 7.8 循环任务模式

`cron.md` 定义基于 Cron 的循环任务（当前：`worktree-stable-enhance`，每 10 分钟一次）。
- 每个循环点启动独立 agent，不与前序任务共享上下文
- 多个 agent 并行编辑可能存在冲突风险
- 取消方式：`CronDelete("<作业 ID>")`，作业 ID 在 `cron.md` 中声明
- `cron.md` 随会话结束自动清理（session 级）
