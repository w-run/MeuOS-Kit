# worktree-shell-utils — Agent 入口

> 其他 Agent 加入此 worktree 时，先读这个文件了解上下文。

## 这是什么

`worktree-shell-utils` 是 MeuOS Kit 的 **Phase 7（用户空间）启动 worktree**。

**目标**：从零实现 `projects/meuos-shell/`（msh）和 `projects/meuos-utils/`（核心工具集），与 Phase 0-5 已完成的 mcc/libc/meow/toolchain/sysroot 衔接，闭环自举链。

**不在本 worktree 范围**：`mcc`、`meuos-libc`、`meow`、`meuos-toolchain`、`meuos-sysroot`、`meuos-buildtools`（各自有独立 worktree）。

## 任务队列

所有任务在 `.issues/INDEX.md` 中按优先级分组：

| 优先级 | 内容 | 文件位置 |
| -------- | ------ | ---------- |
| **P0-skeleton** | 两组件骨架（目录/Makefile/ARCHITECTURE.md/占位 main.c） | `.issues/INDEX.md § P0` |
| **P1-coreutils** | `cat`/`echo`/`true`/`false`/`yes` 简单 IO 类 | `.issues/INDEX.md § P1` |
| **P2-fileutils** | `cp`/`mv`/`rm`/`ls`/`mkdir`/`rmdir` 文件/目录类 | `.issues/INDEX.md § P2` |
| **P3-textutils** | `head`/`tail`/`wc`/`sort`/`uniq` 文本处理类 | `.issues/INDEX.md § P3` |
| **P4-diff/find** | `diff`/`find`/`grep` | `.issues/INDEX.md § P4` |
| **P5-archive** | `tar` + `gzip` 归档/压缩 | `.issues/INDEX.md § P5` |
| **P6-shell-core** | msh POSIX sh 子集（解析+执行+变量+命令替换） | `.issues/INDEX.md § P6` |
| **P7-shell-interactive** | msh 行编辑/历史/Tab/作业控制 | `.issues/INDEX.md § P7` |
| **P8-shell-bash** | msh 可选 bash 兼容层 | `.issues/INDEX.md § P8` |

## 设计原则（快速参考）

1. **§4 禁止事项强约束**：零 glibc 专有符号 / 零 GNU 代码 / 零 autotools / 系统调用直走 `syscall()` 或内联汇编 / 构建可重现
2. **§6.1 三阶段路径**：标准化可用 → 有利特性 → 性能优化
3. **§6.2 参考资源（节省算力）**：Shell → dash/serenityOS Shell；Utils → uutils/BusyBox/serenityOS Utilities。不复制源码，用自己的代码重新实现
4. **§4 简单 Makefile**：每个组件用简单 Makefile 构建，**禁止 autotools/cmake/meson**
5. **§5.2 sysroot 约定**：`MEUOS_SYSROOT` 须指向架构子目录 `sysroot/<arch>`（非顶层）
6. **多调用二进制（BusyBox 风格）是可选**：首期按每工具独立二进制实现，未来可补多调用支持

## 关键文件索引

| 路径 | 内容 |
|------|------|
| `AGENTS.md` | 项目全局规约（会话恢复流程优先度最高） |
| `.issues/AGENT.md` | 本文件，worktree 入口 |
| `.issues/INDEX.md` | 任务队列 + 设计文档 |
| `projects/meuos-utils/ARCHITECTURE.md` | utils 项目结构 + 路线图 |
| `projects/meuos-shell/ARCHITECTURE.md` | msh 项目结构 + 路线图 |
| `projects/meuos-utils/.todo/` | utils 子任务详细设计 |
| `projects/meuos-shell/.todo/` | msh 子任务详细设计 |
| `pkgs/dash/` | dash 0.5.12 构建配方（POSIX sh 参考实现） |
| `reference/` | cproc/QBE/musl 参考（gitignored） |
| `env/` | QEMU 测试环境（已链接自 main） |

## 工作树特殊状态

- 从 `main` 拉出（不含 worktree-stable-enhance 的 .issues/INDEX.md/AGENT.md）
- 拥有全新的 `.issues/AGENT.md` + `.issues/INDEX.md`（本 worktree 专属）
- 不跟踪其他 worktree 的工作，所有变更只 push 到 origin/worktree-shell-utils

## 典型 Agent 启动流程

```sh
# 1. 读 AGENTS.md（项目全局规约）
# 2. 读 .issues/AGENT.md（本入口文件）
# 3. 读 .issues/INDEX.md（任务队列）
# 4. 确认 MEUOS_SYSROOT 已设置（须指向 sysroot/<arch>）
# 5. 选定任务，开始实施
```

## 工作纪律

每次任务执行结束后**必须执行**以下步骤：

### 1. 验证
```sh
make -C projects/meuos-utils check        # 或 projects/meuos-shell/check
make -C projects/meuos-utils check-sysroot-static   # 可选，验证 mcc 路径
```

### 2. 提交变更
```sh
git add -A
git commit -m "<component>: <task-id> <描述>"
# 提交到 worktree-shell-utils，不合并到 main
git push origin worktree-shell-utils
```

### 3. 更新文档
- 完成任务：在 `.issues/INDEX.md` 把状态从 ⏳/🔴 改成 🟢/✅
- 新增任务：追加到 `.issues/INDEX.md` 对应优先级
- 设计决策/踩坑：追加到 `projects/<组件>/ARCHITECTURE.md` 的「实施笔记」段或对应 `.todo/` 文件

### 4. 重读约束

回到本文件 + `.issues/INDEX.md` 设计原则段，防止上下文过长导致对以下约束的记忆衰减：
- §4 禁止事项（glibc/GNU/autotools）
- §6.1 三阶段路径（先标准化、再特性、再优化）
- §6.2 不复制源码，用自己的手重写
- 多调用二进制为可选（每工具独立二进制首期）

## 执行策略

| 任务类型 | 策略 |
|---------|------|
| **工具实现（cat/echo 等独立工具）** | 单文件单 PR，参考 busybox/uutils 对应源码但重写，提交粒度 = 1 个工具 |
| **msh 模块实现（lex/parse/exec/builtin）** | 按依赖 DAG 串行 + 阶段内并行 hy3 无头 agent |
| **跨组件验证** | mcc + sysroot 路径通过 `make -C projects/<comp> check-sysroot-static` 验证 |
| **Makefile 调整** | 保持简单 Makefile（§4），不引入 autotools/cmake |

## 任务 ID 命名规则

格式：`<组件>-<工具/模块>-<修饰>`

- `utils-cat`, `utils-cp`, `utils-grep`, `utils-tar`
- `msh-lex`, `msh-parse`, `msh-exec`, `msh-var`, `msh-builtin`, `msh-history`, `msh-lineedit`, `msh-job`

全局唯一，可直接用作 git 提交引用。

## 工作量估算系数

| 任务类型 | 单任务估算 |
|---------|-----------|
| 简单工具（cat/echo/true） | 1-4 小时 |
| 中等工具（cp/mv/ls/find） | 4-16 小时 |
| 复杂工具（grep/sed/tar/diff） | 1-3 天 |
| msh 词法/语法/执行 各阶段 | 1-2 周/阶段 |
| msh 交互层（readline 替代） | 1-2 周 |
| msh bash 兼容层 | 持续投入，不设截止
