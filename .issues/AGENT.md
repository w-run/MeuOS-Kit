# worktree-stable-enhance — Agent 入口

> 其他 Agent 加入此 worktree 时，先读这个文件了解上下文。

## 这是什么

`worktree-stable-enhance` 是 MeuOS Kit 的长期优化工作分支。
目标：C 工具链彻底完善（6 架构）+ 动态链接 + DWARF 调试信息。

## 任务队列

所有任务在 `.issues/INDEX.md` 中按优先级分组：

| 优先级 | 内容 | 文件位置 |
|--------|------|---------|
| p0-blockers | 阻塞 bug | `.issues/INDEX.md` § 关键路径 |
| p1-core | 核心功能（动态链接/DWARF/基本修补） | `.issues/INDEX.md` § P1 |
| p2-toolchain | 工具链生态（as/ld/辅助工具） | `.issues/INDEX.md` § P2 |
| p3-libc | libc 标准接口 | `.issues/INDEX.md` § P3 |
| p4-devexp | 开发者体验/编译器质量 | `.issues/INDEX.md` § P4 |
| p5-meow | meow 构建系统 | `.issues/INDEX.md` § P5 |
| p6-c23 | C23 标准边缘情况 | `.issues/INDEX.md` § P6 |
| p7-subarch | 子架构/CPU 特性 | `.issues/INDEX.md` § P7 |

## 任务 ID 命名规则

格式：`<组件>-<功能>[-<修饰>]`

- `bug-*` — 阻塞 bug（如 `bug-riscv64-emit`）
- `mcc-*` — 编译器
- `libc-*` — C 库
- `ld-*` — 链接器
- `as-*` — 汇编器
- `meow-*` — 构建系统
- `c23-*` — C23 标准
- `target-*` / `triple-*` — 子架构/三元组
- `specs-*` — 编译参数
- `ci-*` — 测试基础设施

任务 ID 全局唯一，可直接用作 git 提交引用。

## 设计原则（快速参考）

1. **原则零**：不重实现历史。每个工具根据**我们的使用场景**重新设计，不照搬 GNU/Linux 做法
2. **先固自身、再兼容外部**：核心自举链保持纯净，外部兼容走 compat 层
3. **compat 层按来源分目录**：`glibc/`、`gcc/`、`clang/` 等
4. `--specs=meuos` 应该默认（设了 MEUOS_SYSROOT 就自动走 MeuOS 模式）
5. meow 零参数构建（`meow build` 自动检测一切）

## 关键文件索引

| 路径 | 内容 |
|------|------|
| `.issues/INDEX.md` | 完整任务队列 + 设计文档 |
| `.issues/env-symlink.md` | env/ 软链接说明 |
| `projects/mcc/src/driver/msys.c` | mcc .msys sysroot 集成 |
| `projects/mcc/src/driver/main.c` | mcc 驱动入口，--target 解析 |
| `projects/meow/Makefile` | meow 构建，含 make msys 目标 |
| `AGENTS.md` | 项目全局规约（会话恢复流程优先度最高） |
| `projects/meuos-sysroot/ARCHITECTURE.md` | .msys 格式设计 |

## 工作树特殊状态

- `env/` 软链接到 main 分支的 env/（含 qemu 10.1.0），不在此分支跟踪
- 不在本次范围的项目：meuos-utils、meuos-shell(msh)、meuos-buildtools

## 典型 Agent 启动流程

```sh
# 1. 读 AGENTS.md（项目全局规约）
# 2. 读 .issues/AGENT.md（本入口文件）
# 3. 读 .issues/INDEX.md（任务队列 + 设计原则）
# 4. 确认 MEUOS_SYSROOT 设置
# 5. 开始工作
```

## 工作纪律

每次任务执行结束后**必须执行**以下步骤，避免上下文冗余导致遗忘规则：

### 1. 提交变更

```sh
git add -A
git commit -m "<task-id>: <简短描述>"
# 仅提交到 worktree 分支（worktree-stable-enhance），不合并到 main
```

不合并到 main，仅用于追踪变更记录。main 的合并由大喵统一管理。

### 2. 更新文档

- 任务中的决策、踩坑记录更新到 `.issues/INDEX.md` 对应条目
- 新增任务追加到 `.issues/INDEX.md` 对应优先级表
- 完成的任务更新状态（🔴→🟢）

### 3. 重读约束规则

从头重读 `.issues/AGENT.md` + `.issues/INDEX.md` 的「设计原则」部分，防止上下文过长导致遗忘：

- **原则零**：不重实现历史，不照搬 GNU
- 先固自身，走 compat 层
- 不做历史包袱项（`ld-defsym`、`ld-wrap`、`.la` 文件等）

### 4. 下一个任务

从 `.issues/INDEX.md` 选取下一个待办项（按优先级顺序：p0 → p1 → p2...）。

## 执行策略

| 任务类型 | 策略 |
|---------|------|
| **复杂任务**（编译器特性/链接器/ld.so） | 参考 `reference/` 目录的社区实现，理解算法后用我们自己的方式重写。不死扣细节钻牛角尖 |
| **简单重复任务**（枚举源文件/批量修改/测试用例） | 用 shell 脚本自动化，或启动 subagent 并行处理 |
| **跨组件任务**（meow + mcc 联动改动） | 先串行改依赖组件，再并行实现独立部分 |

## 开发模式：Phase-Driven Subagent Dispatch

### 模式选择依据

| 模式 | 适用场景 | 本项目是否适合 |
|------|---------|--------------|
| **Loop** (`/loop`) | 定时轮询/监控 | ❌ 开发任务需要上下文，不适合定时触发 |
| **Subagent** (`Agent run_in_background:true`) | 独立任务并行执行 | ✅ **主模式**。任务的依赖 DAG 拉平后阶段内无交叉 |
| **Team** (`TeamCreate`) | 复杂多角色协作 | ⚠️ 超大任务（如 ld.so 全链路）可用，一般任务 overhead 过高 |
| **单线程主 agent** | 需连续上下文的串行任务 | ✅ 用于阶段内的串行链条 |

### 推荐模式：Phase → Dispatch → Collect → Archive

```
┌─────────────────────────────────────────────────┐
│ Phase Lead（主 agent）                           │
│                                                   │
│  ① 重读约束（AGENTS.md → AGENT.md → INDEX.md）    │
│  ② 规划阶段：选任务 → 识别依赖 → 拉平 DAG         │
│  ③ 执行串行依赖链（直接做，需上下文连续性）          │
│  ④ 分派并行任务（subagent fork model hy3）         │
│  ⑤ 收集结果（TaskOutput）+ 集成验收                │
│  ⑥ 更新文档 + git commit                          │
│  ⑦ 重读约束 → goto ①                               │
└─────────────────────────────────────────────────┘
```

### 具体步骤

#### 步骤 ①-②：规划

从 `.issues/INDEX.md` 选一组任务，确保：

```
Phase N:
  ├── dep-task-A（串行，必须先做）
  │     └── 无依赖，但与后续任务共享上下文 → 主 agent 直接做
  │
  ├── parallel-task-B（并行，与 C 互不依赖）
  ├── parallel-task-C（并行，与 B 互不依赖）
  │     └── 每个 subagent 卡片自包含（含参考路径 + 验收命令）
  │
  └── integration-task-D（依赖 B+C，串行收尾）
```

#### 步骤 ③：串行执行

依赖链上的任务由主 agent 直接完成，因为需要完整的上下文连续性。

#### 步骤 ④：并行分派

```markdown
Agent 工具调用参数：
  subagent_type: "fork"        ← 继承 AGENTS.md 规约和项目上下文
  model: "hy3"                  ← 无头模式，允许多轮对话
  run_in_background: true       ← 不阻塞主 agent
  description: "<task-id>: 简短描述"
  prompt: |
    ## 任务: <task-id>
    ### 范围
    [精确到文件的修改内容]
    ### 参考
    [本仓库同类实现 + 社区参考路径]
    ### 验收
    [可写成 shell 的验收命令]
    ### 完成后
    不提交、不更新文档，通过 TaskCreate/TaskUpdate 更新追踪
    结果通过 SendMessage(recipient:"main") 回报
```

并行数 ≤ 4，避免上下文过大。任务卡片必须自包含，不依赖主 agent 的外部上下文。

#### 步骤 ⑤：收集与集成

```sh
TaskOutput(task_id=...)    # 逐个收集
# 验证每个任务通过验收
# 检查跨任务的一致性（如 API 签名是否匹配）
```

#### 步骤 ⑥：归档

```sh
# 1. 更新 .issues/INDEX.md（状态 ✅）
# 2. git add -A && git commit -m "<task-id>: <描述>"
# 3. 合并 phase 分支（仅 worktree 本地，不 push main）
```

#### 步骤 ⑦：重读约束

回到步骤 ①。每次循环**必须重读**约束文件，因为上下文过长会导致对原则零（不照搬 GNU）、compat 层策略、scope 边界等关键约束的记忆衰减。

### 任务卡片模板

供分派 subagent 时使用：

```markdown
## 任务: <task-id>
**文件**: path/to/file.c
**范围**: 创建/修改 xxx，实现 xxx 功能
**参考**:
  - 本仓库: projects/xxx/src/yyy.c（同类实现）
  - 社区: reference/xxx/（算法参考）
**验收**:
  make -C projects/xxx check 通过
  && ./产物验证命令
**依赖**: <前置任务ID> 已完成
**完成后**: SendMessage(recipient:"main", content="结果摘要")
```

### 工作量估算系数

| 任务类型 | 单任务估算 | 说明 |
|---------|-----------|------|
| emit/isel bug 修复 | 30-60 分钟 | 改几行 emit/iseln，但需要理解 IR 数据流 |
| mt/ld 功能增补 | 1-4 小时 | 链接器改动涉及 ELF 规范查证 |
| libc 函数族 | 0.5-2 小时/族 | 标准接口实现，参考 musl 算法 |
| mcc 标准特性 | 1-8 小时 | 需要理解 C 标准规范 |
| meow 功能 | 2-8 小时 | YAML 解析 + DAG 执行逻辑 |
| as 伪指令 | 0.5-2 小时 | 汇编器解析器改动 |
| DWARF 调试信息 | 4-16 小时 | emit 阶段插点 + ELF 节区生成 |
| ld.so | 8-40 小时 | ELF 加载器系统项目 |
