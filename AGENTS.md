# MeuOS Kit - Agent 初始化 Prompt

> Update: 2026-08-04（精简版：详细参考下放到 `.agents/reference/`，按需读取）
>
> IMPORTANT: 全程思考/回复/文档优先使用简体中文
> [你必须称呼用户为大喵]
>
> **分支与提交策略**：
> - **新任务必须创建分支**，禁止在 `main` 上直接开发（格式：`feat/<描述>`、`fix/<描述>`、`doc/<描述>`）。
> - **worktree 分支**（`worktree-<描述>`）用于长期、跨多组件的联动开发。生命周期长，阶段性成果合并到 `main` 后再继续下一阶段；提交时仍需遵守单组件粒度（`<组件>: <描述>`）；合并到 `main` 前应做全量回归（对应组件 `make check`）。
> - 仅在以下情况可提交到 `main`：一次性修复（typo、编译报错修正）、文档同步、`.todo/` 待办状态更新、纯重构不涉及功能变更。
> - 每次提交前必须跑对应组件的 `make check`，确保不引入回归。
> - 提交信息格式：`<组件>: <描述>`，例如 `mcc: fix va_list alignment on i386`。
> - **完成即推送**：每次提交后直接 `git push`，不积压本地提交。
> - **合后清理**：分支合并到 `main` 后，若无特殊要求，删除本地分支（`git branch -d`），保留远程分支。
>
> **多 Agent 协作规约**：
> - **你不是唯一的工作者**：此项目可能处于多个 Agent 并行工作状态，你只是其中之一。操作共享资源（分支、文件、issues）时需考虑并发冲突。
> - **拉分支 + 工作树**：执行任何任务前，先创建新分支（worktree-<描述>），再通过 git worktree add 在 .codebuddy/worktrees/ 下创建工作树。所有开发在独立工作树中进行。
> - **复杂任务拉团队**：需要跨组件修改或涉及多个文件的任务，使用 TeamCreate 创建团队，按职责分派 Worker。禁止单 Agent 单线程处理大型任务。
> - **拉取最新状态**：开始任务前运行 `git fetch origin && git log origin/main..main` 确认是否有他人已推送的变更。操作 `.todo/` 等共享文件时注意冲突。
>
> **团队工具使用规约**（强制）：
>
> | 工具 | 正确用法 | 禁止 |
> |------|----------|------|
> | `TeamCreate` | `{ team_name: "mkit-xxxx", description: "..." }` | 不允许以组件名直接作为队名 |
> | `Agent` (spawn 队员) | **必须**提供 `name` + `team_name` + `run_in_background`，**不得设置** `subagent_type` | 禁止用 `subagent_type="executor"` 等子代理方式 |
> | `Agent` 的 `name` | 必须是角色名如 `mcc-worker`、`libc-worker` | 禁止用 `general-purpose` 或缺省 |
> | `Agent` 的 `model` | 必须显式指定 `model: "lite"` 或 `model: "reasoning"` | 禁止缺省 |
> | `SendMessage` | 用 `{ type: "message", recipient: "队员名", content: "..." }` 发消息 | 禁止用广播代替一对一私信 |
> | `SendMessage` shutdown | 用 `{ type: "shutdown_request", recipient: "队员名" }` 优雅关停 | 禁止直接中断或忽略队员消息 |
> | `TaskCreate` / `TaskUpdate` | 创建任务后通过 `owner` 分派给队员，队员完成后再 `TaskUpdate` 标记 `completed` | 禁止自己创建任务自己完成，应直接做 |
> | Task 状态流转 | `pending` → 领任务设 `owner` → `in_progress` → `completed` | 禁止跳过 `in_progress` 直接标记完成 |
>
> **团队工作流**：
> 1. `ToolSearch` + `TeamCreate` → 创建团队
> 2. `TaskCreate` → 创建任务卡片（含范围/验收/依赖）
> 3. `Agent` spawn 队员（`name` + `team_name` + `model`）→ 队员自动加入团队
> 4. `TaskUpdate` 分配 `owner` → 队员领任务
> 5. 队员通过 `SendMessage` 汇报成果或卡点
> 6. 队员完成任务后 `TaskUpdate` 标记 `completed`
> 7. 所有队员完成 → 合并分支 → `SendMessage shutdown_request` 关停队员
> 8. `TeamDelete` 清扫团队
>
> **会话恢复流程**（强制要求：新 agent 启动时**必须**按顺序执行以下步骤）：  
> 0. **团队规约确认** — 读取 AGENTS.md 中「团队工具使用规约」表格，确认 spawn/通信/关停规则。**禁止使用子代理（subagent_type）**。
> 1. **读取 IMA 知识库规划文档** — 查询 IMA 知识库中的 MeuOS 规划文档（`search_knowledge_base` → `get_knowledge_list`），阅读标题含"规划"/"计划"/"路线图"/"需求"/"设计"的文档。详见 `.agents/reference/knowledge-mgmt.md` §9.4。
> 2. **子项目上下文加载** — 读目标子项目的 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/<project>/`（待办项），了解项目当前进度。
> 3. **AGENTS.md 规约确认** — 重新确认项目规约（§4 禁止事项、§5 参考索引）和项目状态（`.agents/reference/status.md`）。
> 4. **经验库读取** — 读 `.agents/knowledge/README.md` 索引，了解已沉淀的 git 纪律与缺陷闭环经验。
> 5. **环境检查** — 确认 `MEUOS_SYSROOT` 已设置（须指向 `sysroot/<arch>`，如 `sysroot/x86_64`），宿主编译器和交叉工具链可用。
>
> 各子项目独立维护状态，无全局 STATE 文件。`ARCHITECTURE.md` 是结构/路线图权威来源。
> **待办任务约定**：所有未完成待办统一存放在顶层 `.todo/<project>/`（见 `.todo/README.md`），禁止在 `projects/<name>/.todo/` 下新建。已闭环经验沉淀到 `.agents/knowledge/`。

**项目名称**：MeuOS Kit
**项目定位**：MeuOS Next 的完整自举开发工具集。提供从零自举所需的全部工具：C/C++ 编译器、标准 C 库、构建系统、底层工具链、核心工具集、Shell、TUI 库与自研内核。
**许可**：RFL (Run Free Software License) v1.0

**核心组件**：

- `mcc` / `m++` - 编译器（C99+C11+C23 完整；C++23 主路线图完成）
- `meuos-libc` - 标准 C 库（ISO C11 + POSIX，零 GNU 依赖；compat 层独立归档）
- `meow` - 构建系统（取代 make + autoconf）
- `meuos-toolchain` - 底层工具链（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump）
- `meuos-sysroot` - 单文件 sysroot 系统（.msys 格式）
- `meuos-utils` - 核心工具集（coreutils/diffutils/findutils 完整替代）
- `meuos-shell`（msh）- Shell 终端
- `meuos-buildtools` - 构建工具（m4/bison/flex/gperf）
- `meuos-compress` - 轻量 LZ77 压缩库（libmz.a）
- `meuos-libtui` - 终端 UI 库（纯 C11，零依赖）
- `meuos-kernel` - MeuOS 自研内核（设计/规划中）

**交付对象**：具备系统编程和编译器经验的大型 AI Agent（兆级上下文）。

---

## 1. 项目目标

构建一套完整的开发工具集，使得：

1. 可以从任意 Linux 宿主（有 gcc 或 tcc）自举出全套 MeuOS Kit 工具。
2. 用自举出的 Kit 工具能够编译出 MeuOS Next 的最小 sysroot。
3. Kit 自身可以在 MeuOS Next 环境中自我重建（自举验证通过）。
4. 整个自举链零 GNU 代码、零 LLVM 代码、零 glibc 依赖。
5. MeuOS Next 环境中不依赖任何 GNU 工具（make/autoconf/binutils/coreutils 等）。

---

## 2. 组件规范 → 详见 `.agents/reference/components.md`

各组件详细规范（标准支持、命令行、设计原则）见 `.agents/reference/components.md`，按需读取。

| 组件 | 定位 | 状态权威来源 |
|------|------|--------------|
| meuos-libc | ISO C11 + POSIX.1-2008，零 GNU 依赖；compat 独立归档 | `projects/meuos-libc/ARCHITECTURE.md` |
| mcc/m++ | C99+C11+C23 完整；C++23 主路线图完成，复用 libmcc 后端 | `projects/mcc/ARCHITECTURE.md` |
| meow | 取代 make+autoconf+pkg-config+libtool；YAML/.meow 配方 | `projects/meow/ARCHITECTURE.md` |
| meuos-toolchain | as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump | `projects/meuos-toolchain/ARCHITECTURE.md` |
| meuos-sysroot | .msys 单文件 sysroot 系统（libmsys+mkmsys+msysctl） | `projects/meuos-sysroot/ARCHITECTURE.md` |
| meuos-utils | coreutils/diffutils/findutils 完整替代 | `projects/meuos-utils/ARCHITECTURE.md` |
| meuos-shell | 完整 Shell（POSIX sh + bash 兼容 + zsh 插件/主题） | `projects/meuos-shell/ARCHITECTURE.md` |
| meuos-buildtools | m4/bison/flex/gperf 构建工具 | `projects/meuos-buildtools/ARCHITECTURE.md` |
| meuos-compress | 轻量 LZ77 压缩库（libmz.a） | `projects/meuos-compress/ARCHITECTURE.md` |
| meuos-libtui | 纯 C11 终端 UI 库（零 ncurses/readline 依赖） | `projects/meuos-libtui/ARCHITECTURE.md` |
| meuos-kernel | MeuOS 自研内核（设计文档见 projects/meuos-kernel/） | kernel-plan 工作树 |

---

## 3. 自举流程 → 详见 `.agents/reference/bootstrap.md`

组件构建依赖链（从下往上，下层先构建）：

```
meuos-buildtools (m4/bison/flex/gperf)
  ↑ mcc + meuos-libc
meuos-utils / meuos-shell
  ↑ mcc + meuos-libc + meow
meow
  ↑ mcc + meuos-libc + meuos-toolchain (mt/as, mt/ld)
meuos-toolchain (as/ld/ar/ranlib/nm/objdump/readelf/strip/objcopy)
  ↑ mcc + meuos-libc + meuos-sysroot
meuos-sysroot
  ↑ 宿主 cc 或 mcc
meuos-libc
  ↑ mcc
mcc
  ↑ 宿主 cc 或自身
```

自举阶段：**Phase 0** 准备 → **Phase 1** 诞生 mcc → **Phase 2** 诞生 meuos-libc → **Phase 3** 诞生 meow → **Phase 4** 自举验证 → **Phase 5** 工具链完善 → **Phase 6** 构建工具 → **Phase 7** 用户空间。每个阶段都要验证。详见 `.agents/reference/bootstrap.md`。

---

## 4. 禁止事项（强约束）

- **禁止**任何 glibc 专有头文件、符号、宏出现在 meuos-libc 核心或 mcc 源码中。
- **禁止**引入 LLVM/Clang 或 GCC 的任何代码。
- **禁止**使用 autotools、cmake、meson 作为 Kit 自身的构建系统（Kit 自身组件必须用简单 Makefile 或 shell 脚本构建）。
- **禁止**系统调用通过 libc 封装，必须直接 `syscall()` 或内联汇编。
- **禁止**预编译二进制提交到仓库（宿主 bootstrapper 除外）。
- **禁止**在 MeuOS Next 环境中依赖 GNU 工具（make/autoconf/binutils/coreutils/bash/m4/bison/flex 等）。
- **要求**构建可重现（无时间戳、无绝对路径硬编码）。

---

## 5. 项目组织 → 详见 `.agents/reference/organization.md`

### 5.1 目录结构（摘要）

```
MeuOS-Kit/
├── AGENTS.md               项目规约（本文件，harness 自动加载）
├── .agents/                项目级 Agent 资源（统一管理）
│   ├── knowledge/          全局记忆（经验库：纪律/缺陷闭环，进 git）
│   ├── reference/          详细参考（组件规范/自举/构建命令/状态速查等）
│   ├── skills/             技能（cross-test/ima-skill/mkit-bootstrap/c11-check/syscall-gen）
│   └── worktrees/          独立工作树
├── .todo/                  项目待办（唯一待办来源，按项目子目录）
├── README.md               项目说明与构建方法
├── bootstrap.sh            Phase 0–5 全流程自举脚本
├── projects/<name>/        各组件（见 §2 表）
├── env/                    QEMU 多架构测试环境（qvm 管理器）
├── pkgs/                   meow 构建配方（.meow 格式）
├── sysroot/                安装目标根文件系统（按架构分目录）
└── reference/              cproc/QBE/musl 只读参考源（gitignored）
```

完整目录结构、构建约定（`MEUOS_SYSROOT`/`ARCH=`）、QEMU 环境（qvm）与 Issue/TODO 导航系统见 `.agents/reference/organization.md`。

---

## 6. 实现策略与任务编排 → 详见 `.agents/reference/strategy.md`

**实现路径**（三阶段）：标准化可用 → 有利特性 → 性能优化。组件间可利用内部知识做特化实现（mcc→mt/as 生成最优汇编、meow→mcc 针对性调度等），但对外标准兼容性不妥协。

**参考资源**：优先参考成熟社区实现（musl/cproc/QBE/chibicc/cxx-frontend/aburi，已 vendored 到 `reference/`），读参考实现理解算法，再用自己的手写出来。详见 §6.2。

**任务编排**（§7）：任务拆细颗粒度（单文件/单函数）、任务卡片五要素（ID/范围/参考来源/验收标准/依赖）、线性单向任务流、阶段归档必须 `make check` 通过、参考来源收集（架构差异对照表）、循环任务（cron.md）。详见 `.agents/reference/strategy.md`。

---

## 7. 构建与测试命令 → 详见 `.agents/reference/build-reference.md`

最常用命令（完整速查见 reference）：

```sh
# 设置 sysroot（必须）
export MEUOS_SYSROOT=/workspace/MeuOS-Kit/sysroot

# 构建组件
make -C projects/mcc                          # 构建 mcc
make -C projects/meuos-libc                   # 构建 x86_64 libc 核心
make -C projects/meow                         # 构建 meow
make -C projects/meuos-toolchain              # 构建 9 个工具

# 测试
make -C projects/mcc check                    # mcc 门禁
sh projects/mcc/test/verify-all.sh            # mcc 全套回归（19 项）
make check                                    # 宿主全套回归
```

构建/测试/自举/跨架构/调试完整命令见 `.agents/reference/build-reference.md`。

---

## 8. 知识库管理 → 详见 `.agents/reference/knowledge-mgmt.md`

- **本地经验库** `.agents/knowledge/`：进 git 的项目经验（feedback_* 纪律、project_* 缺陷闭环）。新 agent 启动读 README 索引。
- **IMA 知识库**（`ima-skill`）：未来才实现的需求/设计/规划/会议记录（不适合进代码仓库的内容）。
- **核心判据**：未来才实现 → IMA；已经实现的规格和约束 → 代码仓库（ARCHITECTURE.md）。
- **Agent 启动第一规约**：主动读取 IMA 规划文档（§9.4 流程）。

详细分工边界、ima-skill 使用、配置要求、启动读取流程见 `.agents/reference/knowledge-mgmt.md`。

---

## 9. 项目状态速查 → 详见 `.agents/reference/status.md`

- **已完成里程碑**：mcc C11/C23 完整、6 后端内置、arm 移植、libc 多架构验证、meow、toolchain 9 工具、Phase 4/5 自举验证、.msys v2、.meow 宏系统等。
- **待启动/进行中**：m++ 后续阶段、meuos-buildtools、meuos-utils/msh 骨架、mt DWARF、arm-multiver。
- **架构支持矩阵**：x86_64/aarch64/riscv64/i386/loongarch64/arm 各组件支持状态。
- **文档索引 + CI 管道**。

> ⚠️ 本文件易过时，权威状态以 `.todo/`、`.agents/knowledge/` 与各组件 ARCHITECTURE.md 为准。

---

## 10. Issue/TODO 导航 → 详见 `.agents/reference/organization.md` §11

**核心系统只有两个**：项目待办（`.todo/`）+ 全局记忆（`.agents/knowledge/`）。

| 信息类型 | 位置 | 说明 |
|---------|------|------|
| 项目待办（未完成） | `.todo/<project>/` | 唯一待办来源，按项目子目录 |
| 全局记忆（已闭环经验） | `.agents/knowledge/` | 缺陷闭环、纪律、修复方案 |
| 组件结构/路线图 | `projects/<name>/ARCHITECTURE.md` | 组件权威 |
| 组件移植契约 | `projects/<name>/PORTING.md` | 多架构 ABI |
| 全局状态速查 | `.agents/reference/status.md` | 聚合摘要 |

读取优先级：`.todo/`（待办）→ `.agents/knowledge/`（经验）→ status.md → 组件 ARCHITECTURE。
