---
name: "mkit-session-import"
description: "从一个已存在的 CodeBuddy 会话 jsonl 恢复上下文，把该会话几乎当作团队成员『导入』当前工作。teammate/子 agent 启动后，指定会话 id 即可先去读指定 jsonl 拿到历史背景再动手。Invoke when user says: import session, load session context, resume from session, work like another session, 根据某个会话继续, 读取会话.<id>, treat session X as a teammate."
---

# MeuOS Session Import — 从 jsonl 恢复会话上下文

把另一个 CodeBuddy 会话（存在 `/root/.codebuddy/projects/workspace-MeuOS-Kit/*.jsonl`）的历史，转化为本 agent 的启动上下文。

CodeBuddy 没有「拿外部会话 id 直接 attach 成 teammate」的原生机制；本 skill 补上中间一步：**会话 id → agent 亲自读 jsonl → 复述确认 → 开始干活**。效果上把那个会话“当作战友接进来”。

## When to invoke

- 用户/团队负责人给一个 `session-id`，让你“先去读它”、“按它继续”、“import 它为队友”。
- 你想接续某个之前会话的进度、决策、交接内容。
- 需要了解另一个会话当时做了什么（工具调用、关键结论）。

## Procedure

### 1. 若有会话 id，先定位会话

```bash
# 确认会话 jsonl 存在（id 通常形如 4e739750-4073-4bd4-af05-7acae4cb613e）
ls /root/.codebuddy/projects/workspace-MeuOS-Kit/<SESSIONID>.jsonl
```

若没有现成 id，先列出可选会话再选（标题/时间/消息数/模型一目了然）：

```bash
python3 .agents/tools/session-import.py list
```

### 2. 提取该会话上下文（读取器）

```bash
python3 .agents/tools/session-import.py show <SESSIONID>
```

输出：原始标题、参与模型、时间窗、**文本消息脉络**、**主要工具调用统计**。读这个输出 = 恢复上下文。长会话会被裁剪到约 6000 字符（脉络摘要，足够接手）。

> 直接读原始 jsonl 也可，但用脚本更省 token 且聚焦。若会话需完整工具细节，可再针对性地 `grep` 原始 jsonl。

### 3. 复述确认（重要）

读完后，**用自己的话复述**：该会话的目标、已做到哪、卡在哪、下一步该做什么。若和当前任务有关联，明确衔接点，再开始动手。这一步防止"读了却理解偏"。

### 4. 尊重交接纪律

若该会话含交接内容（HANDOFF/任务清单/团队语义分工，参考 `mkit-dev-ops`），按其中约定继续：
- 遵守 `.todo/<project>/` 待办状态与 `.agents/knowledge/` 既有结论。
- 若它是另一个并行会话，改动共享文件/分支时按 AGENTS.md 多 Agent 协作规约（worktree 分支、避免 stash/reset、文件级 git add）。

## Hard constraints

- 只读该会话 jsonl，**不修改**它（`session-import.py` 是只读的）。
- 不要把本 skill 当作外部会话的“思维续命”——它是上下文载具，你的判断属于当前会话。
- 涉及并发/共享资源时，遵守 AGENTS.md §4 与多 Agent 协作规约。
