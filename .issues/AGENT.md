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
