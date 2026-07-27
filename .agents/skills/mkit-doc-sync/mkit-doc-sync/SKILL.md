---
name: "mkit-doc-sync"
description: "文档同步收尾技能。在任务完成、提交之前调用，确保代码变更后文档不被落后。自动检测修改的文件并提示需要同步的文档清单。"
---

# mkit-doc-sync — 文档同步收尾技能

> 本技能是 AGENTS.md §7.6（阶段归档）的自动化助手，在每次代码变更后、提交之前运行。
> 核心原则：**代码变更一旦完成，相关文档必须同步更新，不可遗留到下一次任务。**

---

## 何时触发

以下场景**必须**调用本技能：

1. **提交之前**（`git commit` 前）：检查当前变更是否涉及文档落后的文件
2. **阶段完成时**（AGENTS.md §7.6 归档步骤）：验证文档已同步后再提交
3. **添加新架构/新组件后**：架构表、支持矩阵、README 必须同步更新
4. **添加新功能/新接口后**：对应的 ARCHITECTURE.md、.todo 必须更新
5. 用户明确说"同步文档"、"更新文档"、"收尾一下"、"提交前检查"、"mkit-doc-sync"

---

## 输入

| 输入 | 来源 | 说明 |
|------|------|------|
| Git diff | `git diff --stat HEAD` | 当前未提交的变更文件清单 |
| 当前分支 | `git branch --show-current` | 确认分支类型（feat/fix/doc/worktree） |
| 变更文件类型 | `git diff --name-only HEAD` | 检测修改/新增/删除的文件 |

---

## 工作流

### Step 1: 收集变更信息

```sh
# 获取变更文件清单
CHANGED=$(git diff --name-only HEAD)
NEW=$(git diff --name-only HEAD --diff-filter=A)
MODIFIED=$(git diff --name-only HEAD --diff-filter=M)
DELETED=$(git diff --name-only HEAD --diff-filter=D)
```

### Step 2: 检测变更类别 → 确定需同步的文档

| 如果变更涉及... | 需要同步的文档 |
|---|---|
| **`src/target/<arch>/`** 新增或修改 | `projects/mcc/ARCHITECTURE.md`（后端清单） |
| **`src/target/<arch>/`** + **新 include 头** | `projects/mcc/.todo/<arch>.md`（新建验收清单） |
| **`projects/mcc/`** | `projects/mcc/ARCHITECTURE.md`（§2 目录树 + §6 build 后端数） |
| **`projects/meuos-libc/crt/<arch>/`** 或 **`src/arch/<arch>/`** | `projects/meuos-libc/ARCHITECTURE.md`（crt/arch 目录树） |
| **`projects/meuos-libc/PORTING.md`** 中的架构状态 | 检查该架构的实际代码是否已和文档一致 |
| **`projects/meuos-toolchain/src/target/<arch>/`** | `projects/meuos-toolchain/ARCHITECTURE.md`（架构表格） |
| **`projects/meuos-toolchain/`** | `projects/meuos-toolchain/ARCHITECTURE.md`（阶段表） |
| **`projects/meuos-sysroot/`** | `projects/meuos-sysroot/ARCHITECTURE.md` + `.todo/msys.md` |
| **`projects/meow/`** | `projects/meow/ARCHITECTURE.md`（文件清单 + 功能状态） |
| **新增架构**（跨 3 个组件的 target 目录） | `README.md`（组件表 + 快速开始）+ `.todo`（架构清单）+ `AGENTS.md`（§10 支持矩阵） |
| **新增组件**（新 `projects/<name>/` 目录） | `README.md`（组件表）+ `AGENTS.md`（§2 组件规范 + §5.1 目录结构 + §10 状态表） |
| **删除旧代码/旧文件** | 对应的 `.todo` 标记完成 + 清理 ARCHITECTURE.md 中的过期引用 |
| **新增 .todo 完成项** | 对应 `.todo` 文件 `[ ]` → `[x]` + 需要的话更新 ARCHITECTURE.md 状态表 |

### Step 3: 检查每个需同步的文档

对每个被检测出的目标文档，执行以下检查：

```sh
# 模板：检查文档中的关键数字/状态是否匹配代码
# 例如：检查 ARCHITECTURE.md 中提到多少个后端
BACKENDS_IN_DOC=$(grep -c 'src/target/\w*/' projects/mcc/ARCHITECTURE.md)
BACKENDS_IN_CODE=$(ls -d projects/mcc/src/target/*/ | wc -l)
if [ "$BACKENDS_IN_DOC" -ne "$BACKENDS_IN_CODE" ]; then
  echo "⚠️  文档后端数 ($BACKENDS_IN_DOC) ≠ 代码后端数 ($BACKENDS_IN_CODE)"
fi

# 检查 .todo 中的 [ ] 项是否有对应的代码已实现
TODOS=$(grep '\[ \]' .todo 2>/dev/null | wc -l)
echo "ℹ️  根 .todo 中还有 $TODOS 项待完成"
```

### Step 4: 同步文档

对于每个需要更新的文档，执行以下修改（参考 AGENTS.md 和对应 ARCHITECTURE.md 的格式）：

1. **ARCHITECTURE.md**：更新目录树中的文件清单、状态表、后端数量
2. **PORTING.md**：更新状态表行、完成时态改写未来时态
3. **.todo**：将完成项标记为 `[x]`，必要时新增待办项
4. **README.md**：更新组件表和快速开始命令
5. **AGENTS.md**：§10 项目状态速查表中的里程碑和架构支持矩阵

### Step 5: 验证文档一致性

```sh
# 验证文档中提到的架构数 = 代码中实际的架构数
# 验证文档中提到的阶段状态 = 代码中的实现状态
# 验证 .todo 中的 [x] 项确实有对应代码实现
```

### Step 6: 输出摘要

输出一个结构化摘要，列出：
- 哪些代码文件被修改
- 哪些文档文件被同步更新
- 哪些文档不需要更新（已是最新）
- 如果还有未同步的文档，列出警告

---

## 常见的文档同步模式

### 模式 A：新增架构

当为 mcc/meuos-libc/mt 三者之一新增 target 目录时，**必须**同步以下全部文档：

| 文档 | 需要改什么 | 检查点 |
|------|-----------|--------|
| `projects/mcc/ARCHITECTURE.md` | §2 include/ 加 `arch.h`，`src/target/<arch>/` 目录树 | 4 个后端文件（targ/abi/isel/emit） |
| `projects/mcc/.todo/<arch>.md` | 新建验收清单 | 已完成项用 `[x]`，待办用 `[ ]` |
| `projects/meuos-libc/ARCHITECTURE.md` | §2 crt/ + src/arch/ 目录树 | crt1 + syscall + atomic + setjmp + sigreturn + thread_clone + tls |
| `projects/meuos-libc/PORTING.md` | §1 状态表 + §4 实现要点 + §5 移植顺序 | 状态从"计划"→"已完成" |
| `projects/meuos-toolchain/ARCHITECTURE.md` | 架构表格 + 阶段表 | as encoder + ld reloc |
| `.todo`（根目录） | 架构清单 + P4 条目 | "五架构"→"六架构"等 |
| `README.md` | 核心组件表 + QEMU arch + 快速开始 | 所有组件和命令 |
| `AGENTS.md` | §10 支持矩阵 + 架构依赖表 | 6 个字段全部正确 |
| `env/.todo/<arch>.md` | 新建 QEMU 环境状态 | qemu-user/qemu-system/rootfs |

### 模式 B：新增 .c 文件（非新架构）

| 变更 | 需更新 |
|------|--------|
| `src/stdio/` 新增文件 | `projects/meuos-libc/ARCHITECTURE.md` §3 模块职责 |
| `src/syscall/` 新增 .c | 无需更新文档（syscall 按约定自动发现） |
| `src/opt/` 新增 pass | `projects/mcc/ARCHITECTURE.md` §2 目录树 + §3 模块职责 |
| `src/driver/` 新增功能 | `projects/mcc/ARCHITECTURE.md` §4 Key Files + §8 渐进清理 |

### 模式 C：删除旧代码

| 删除 | 需更新 |
|------|--------|
| `.legacy` 文件 | 无需（本身就是清理的一部分） |
| 旧版 TODO | 删除对应 `.todo` 文件，或在文件中标记 `[x]` |
| 过时架构引用 | 更新所有 ARCHITECTURE.md + PORTING.md 中的引用 |

### 模式 D：BUG 修复

| 修复范围 | 需更新 |
|---------|--------|
| 单文件 fix | 通常无需更新文档（除非改了公开 API） |
| ABI/调用约定修复 | `projects/meuos-libc/PORTING.md` §4 对应架构的注意事项 |
| 工具链核心 bug | `.todo` 中标记 `[x]` + 可选更新 ARCHITECTURE.md |

### 模式 E：知识库（IMA）文档同步

当代码变更涉及以下内容时，考虑同步到 IMA 知识库中的 `MeuOS-Kit-知识文档`：

| 触发条件 | 建议操作 |
|---------|---------|
| 新增架构 | 更新 vN+1 版本并上传到知识库 |
| 新增里程碑 | 更新已完成里程碑清单 |
| 代码量大幅变化 | 更新代码量统计表格 |
| 已知局限变化 | 更新已知局限表 |

---

## 硬约束（引用 AGENTS.md §4）

- 禁止修改 `reference/` 目录下的任何文件
- 禁止修改 .gitignore 中已跟踪的文件
- .todo 完成项必须用 `[x]` 标记，不可直接删除
- ARCHITECTURE.md 只更新状态和清单，不重写设计说明
- PORTING.md 中的架构状态表必须与实际代码一致

---

## 边界情况

- **已完成但未标记的 TODO**：如果代码已实现但 TODO 仍是 `[ ]` → 标记为 `[x]`
- **旧文档引用已删除的代码**：ARCHITECTURE.md 中提到已不存在的文件 → 删除引用
- **文档中的数字不对**：如 "5 targets" 但代码是 6 个 → 更新数字
- **无变更时调用**：输出"无需同步"并退出，不创建空提交
- **worktree 分支的阶段性提交**：不要求全量文档同步，只更新本次涉及的部分

---

## 输出布局

执行完毕后输出以下格式的摘要：

```
## mkit-doc-sync 摘要

### 代码变更
- projects/mcc/src/target/arm/ (4 files, +17KB) — 新增 ARM 后端

### 已同步的文档
- [x] projects/mcc/ARCHITECTURE.md — 添加 arm.h + target/arm/ 目录树 + "6 targets"
- [x] projects/mcc/.todo/arm.md — 新建验收清单

### 无需更新的文档（已是最新）
- [x] projects/meuos-toolchain/ARCHITECTURE.md — 已验证

### ⚠️ 警告（如有）
- projects/meuos-libc/PORTING.md 中 armv7 状态仍为"强烈建议新增"，实际代码已完成
```

---

## 与 AGENTS.md §7.6 阶段归档的关系

本技能覆盖 AGENTS.md §7.6 归档步骤中的 **第 2 步（更新 .todo）** 和 **第 3 步（更新 ARCHITECTURE.md）**，同时补充了 README.md、PORTING.md、AGENTS.md §10 等辅助文档的同步。

执行顺序：
1. 代码修改完成
2. 运行 `make check`（AGENTS.md §7.6 第 1 步）
3. **调用 mkit-doc-sync**（同步文档）
4. git 提交（AGENTS.md §7.6 第 4 步）
5. 合并到 main（AGENTS.md §7.6 第 5 步）

---

## 注意事项（经验总结）

1. **先 diff 再修改**：不要无差别更新所有文档，只更新被代码变更影响的文档
2. **数字核对**：ARCHITECTURE.md 中的后端数量、文件数、行数等数字最容易过期
3. **状态表优先**：ARCHITECTURE.md 和 PORTING.md 中的状态表是最大信息密度区域，优先核对
4. **.todo 标记**：代码完成但 .todo 仍是 `[ ]` 是最常见的文档遗漏
5. **commit message 格式**：文档同步提交信息格式 `doc: <同步内容>（<文件清单>）`
