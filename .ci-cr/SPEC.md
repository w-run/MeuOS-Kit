# MeuOS Kit 自动化 CI/CR 循环工作流需求规格书

**文档版本**：v1.0
**更新日期**：2026-07-23
**适用项目**：MeuOS Kit
**核心约束**：严格遵守 `AGENTS.md` 所有规范（零 GNU/glibc、原子任务卡片、禁止回溯）

---

## 1. 项目背景与目标

### 1.1 背景

当前 MeuOS Kit 已具备成熟的状态管理（Git Worktree + `.todo`）和编码规范（`AGENTS.md`）。为实现**完全无人干预**的迭代开发，需构建一套闭环自动化系统，将“高推理模型（大脑）”的规划能力与“轻量免费模型（Hy3）”的执行能力分层解耦。

### 1.2 核心目标

1. **零人工干预**：从任务规划、代码编写、验收测试到代码审查，全自动流转。
2. **算力极致节约**：成功路径上大脑（GLM/DeepSeek Pro）零 Token 消耗；仅失败/审查时启用。
3. **高韧性**：支持 API 配额耗尽、网络闪断、进程崩溃后的**自动检测与无缝接续**。
4. **安全合规**：严格执行 `AGENTS.md` §4 约束，杜绝 GNU/glibc 代码污染。

---

## 2. 目录结构规范（`.ci-cr/`）

在项目根目录创建 `.ci-cr/`，作为整套工作流的“控制面板”和“黑匣子”。

```text
.ci-cr/
├── README.md                          # 本工作流的使用与维护说明
├── config/
│   ├── settings.base.json             # CodeBuddy CLI 基础配置（模型、超时）
│   ├── model_router.yaml              # 模型优先级与余额阈值配置
│   └── acceptance_manifest.json       # 验收脚本与任务类型的映射表
├── scripts/
│   ├── driver_daemon.sh               # 主驱动守护进程（入口，由 cron/systemd 触发）
│   ├── balance_probe.py               # 多模型余额探针（支持 CodeBuddy/ArkCLI/Codex）
│   ├── task_executor.py               # 调用 CodeBuddy CLI 执行单任务卡片
│   ├── milestone_reviewer.py          # 里程碑 Git Diff 审查器（调用大脑）
│   └── recovery_cleaner.py            # 僵尸锁/超时会话清理器
├── state/                             # 运行时状态（可丢失，用于恢复）
│   ├── locks/                         # 原子任务锁 (touch .lock/task-xxx.lock)
│   ├── sessions/                      # 会话映射表 (task_id -> session_id)
│   ├── balance_snapshots/             # 余额快照 (latest.json)
│   └── retry_counters/                # 任务重试计数
├── acceptance/                        # 标准化验收脚本（大脑制定，脚本执行）
│   ├── libc_arch.sh                   # 对应 §7.2 验收标准
│   ├── compiler_sanity.sh
│   └── common_checks.sh               # 禁止符号检查 (grep -r "glibc" ...)
├── templates/                         # 任务卡片与提示词模板
│   ├── task_card.json                 # 大脑输出给执行层的标准格式
│   └── fix_prompt.txt                 # 失败时喂给大脑的极简修正提示词模板
└── logs/                              # 运行日志（按日期/会话归档）
    └── YYYY-MM-DD/
        ├── driver.log
        ├── exec_hy3.log
        └── brain_audit.log
```

---

## 3. 核心工作流拓扑（状态机）

系统采用 **“扫描 -> 规划 -> 执行 -> 验收 -> 反馈/审查”** 五步循环状态机。

### 3.1 启动触发（Driver Daemon）

- **触发方式**：由 Cron 或 Systemd Timer 每 **5 分钟** 触发一次 `scripts/driver_daemon.sh`。
- **启动自检**：
    1. 检查 `state/locks/` 是否有僵尸锁（超时 > 30分钟）→ 调用 `recovery_cleaner.py` 清除。
    2. 调用 `balance_probe.py` 获取所有模型余额快照，写入 `state/balance_snapshots/latest.json`。
    3. 根据余额和 `config/model_router.yaml` 决定本轮“大脑”人选。

### 3.2 状态流转逻辑

| 当前状态      | 触发条件                         | 执行动作                                                                                                   | 下一状态               |
| :------------ | :------------------------------- | :--------------------------------------------------------------------------------------------------------- | :--------------------- |
| **IDLE**      | 扫描发现`.todo/` 存在 `[ ]` 任务 | 调用选中的大脑（GLM/DS）生成 JSON 任务卡片并存入`.plan/`                                                   | **PLANNING**           |
| **PLANNING**  | 卡片生成成功                     | 调用`task_executor.py` 下发 Hy3 执行；创建 `.lock` 与 `session_id` 映射                                    | **EXECUTING**          |
| **EXECUTING** | Hy3 执行完毕（成功/失败）        | **成功**：运行 `acceptance/` 对应脚本 → 若 `exit 0`，更新 `.todo`，`git commit`，删除锁                    | **IDLE**（或下一任务） |
| **EXECUTING** | Hy3 执行完毕（失败）             | **失败**：截取验收脚本 `stderr` 最后 3 行，调用紧急大脑（Codex/Minimax）生成 `sed` 补丁                    | **HOTFIXING**          |
| **HOTFIXING** | 补丁应用成功且验收通过           | 提交本次修正，删除锁                                                                                       | **IDLE**               |
| **HOTFIXING** | 补丁应用失败 / 重试 > 3 次       | **禁止回溯**（§7.3）。放弃该任务卡片，在 `.todo` 标记 `[!]`，提交 Issue 摘要到 `.state/abandoned/`，删除锁 | **IDLE**               |
| **MILESTONE** | 某一子项目所有`.todo` 标记 `[x]` | 触发`milestone_reviewer.py`：提取 `git diff main...HEAD`，喂给大脑做架构级审查                             | **REVIEWING**          |
| **REVIEWING** | 审查通过 (`APPROVED`)            | 自动合并入`main`，销毁 Worktree                                                                            | **IDLE**               |
| **REVIEWING** | 审查驳回（带修正补丁）           | 生成新的修正任务卡片（`fix-review-xxx`）进入 `PLANNING` 队列                                               | **PLANNING**           |

---

## 4. 关键模块详细需求

### 4.1 余额感知调度器（`balance_probe.py`）

- **输入**：`config/model_router.yaml`（含各模型 API 端点、阈值）。
- **输出**：`state/balance_snapshots/latest.json`。
- **逻辑**：
    - CodeBuddy (DS Pro)：通过 `codebuddy -p 'ping' --format json` 解析响应头或错误信息中的 `X-RateLimit-Remaining`。
    - ArkCLI：调用 `ark balance` 解析 JSON。
    - Codex (Minimax)：解析 `codex api user/info`。
- **门控策略**：
    - 若主力模型（DS Pro）余额 > 20%：承担规划与审查。
    - 若 5% < 余额 < 20%：仅承担审查（规划转交 ArkCLI）。
    - 若余额 < 5%：完全休眠，驱动仅执行已有 `.todo` 中的 Hy3 任务（不生成新计划）。
    - 若查询接口超时：保守假设余额充足，但强制降级为单任务并发（防超额）。

### 4.2 标准化验收执行器（`acceptance/`）

- 验收脚本由大脑（GLM）在规划时制定，并**固化存储**至 `acceptance/` 目录。
- 执行器 `task_executor.py` 必须按以下顺序操作：
    1. 运行 `acceptance/common_checks.sh`（全局禁止检查，如检测 `glibc` 符号）。
    2. 运行任务卡片中指定的专用验收脚本。
    3. 只有两步均 `exit 0`，才判定任务成功。
- **特殊机制**：验收脚本的 `stdout` 默认丢弃，`stderr` 截断保存用于故障恢复。

### 4.3 会话管理恢复器（集成 `task_executor.py`）

- **执行前**：检查 `state/sessions/` 是否存在该 `task_id` 的历史 `session_id`。
    - 若有，调用 `codebuddy --resume ${session_id} -p "继续完成未竟任务"`。
    - 若无，调用 `codebuddy -p "..." --output-format json` 新建会话，并捕获 `session_id`。
- **持久化**：将 `task_id` -> `session_id` 映射写入 `state/sessions/mapping.json`。
- **超时清理**：`recovery_cleaner.py` 定时扫描，若会话创建超过 **2小时** 且未关闭，强制调用 `codebuddy --session-close ${session_id}` 并清除映射。

### 4.4 里程碑差异化审查（`milestone_reviewer.py`）

- **触发**：当 `scripts/driver_daemon.sh` 检测到某子项目（如 `meuos-libc`）的 `.todo` 全部完成。
- **操作**：
    ```bash
    git diff main...HEAD -- . ':!*.todo' ':!.ci-cr/*' > /tmp/milestone.diff
    ```
- **大脑提示词（极简）**：
    > 这是 `{component}` 里程碑的 Git Diff。请检查：
    >
    > 1. 是否引入 GNU/glibc 依赖（对照 §4）？
    > 2. ABI 调用约定是否符合目标架构？
    > 3. 内存泄漏或未初始化风险？
    >    输出：`APPROVED` 或 直接给出 `git apply` 可用的修正补丁。
- **自动执行**：若大脑输出补丁格式，`milestone_reviewer.py` 自动 `git apply` 并触发二次编译验证。

---

## 5. 异常处理与自愈机制

| 异常场景                | 处理策略                                                                                                                                         | 是否唤醒大脑                         |
| :---------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------- | :----------------------------------- |
| **API 网络闪断**        | `task_executor.py` 重试 3 次（指数退避），若成功则继续；若失败，保留 `session_id`，等待 5 分钟后下一轮驱动重试。                                 | **否**（用尽重试后才报错）           |
| **模型配额耗尽（429）** | 立即切换备用模型（按`model_router.yaml` 降级），若全部耗尽，放弃本轮规划，生成 `[!]` 任务，等待人工充值（但执行队列中已有的 Hy3 任务继续运行）。 | **否**（脚本自决）                   |
| **验收脚本无法修复**    | 允许 Hy3 在 Hotfix 阶段失败 3 次。3 次后放弃该任务卡片，**不回溯**，直接在 `.todo` 标记 `[!]` 并记录错误上下文到 `.state/abandoned/`。           | **是**（最后一次上报给大脑记录日志） |
| **驱动进程被 kill**     | 重启后运行`recovery_cleaner.py`：扫描 `state/locks/`，对于超时锁，直接删除并将对应任务卡片重置为 `[ ]`（重新规划）。                             | **否**                               |

---

## 6. 配置管理规范（`config/`）

### 6.1 `settings.base.json`（CodeBuddy 基线）

```json
{
    "model": "hy3",
    "allowedTools": ["Read", "Edit"],
    "disallowedTools": ["Bash", "Write", "Glob"],
    "permissionMode": "bypassPermissions",
    "systemPrompt": "严格遵守 MeuOS Kit AGENTS.md。只修改指定文件，禁止引入 glibc/GNU 代码。"
}
```

### 6.2 `model_router.yaml`（调度策略）

```yaml
models:
    brain_primary:
        provider: codebuddy
        model: deepseek-v4-pro
        min_balance_ratio: 0.2
    brain_fallback:
        provider: arkcli
        plan: coding-plan
        min_balance_ratio: 0.1
    brain_emergency:
        provider: codex
        model: minimax
        min_balance_ratio: 0.0 # 哪怕剩一点也能用，但限制上下文长度
    executor:
        provider: codebuddy
        model: hy3
        # 免费，无余额检查

thresholds:
    critical_balance: 0.05 # 低于此阈值，大脑完全休眠
    session_ttl_minutes: 120
    max_hotfix_retries: 3
```

---

## 7. 验收标准（系统上线要求）

1. **端到端无人测试**：模拟从 `feat/riscv64` 分支启动，系统自动完成 10 个 libc 移植任务，期间人为断网 2 次，观察系统是否在 30 分钟内自动恢复并完成所有任务。
2. **零 GNU 污染审计**：自动运行 `grep -r "glibc\|GNU" src/ --exclude-dir=reference`，确保无新增违规代码。
3. **大脑 Token 消耗统计**：监控 7 天运行周期，大脑消耗 Token 应仅占总 Token 消耗的 **< 5%**（其余由 Hy3 免费额度承担）。
4. **会话恢复成功率**：人为终止 `codebuddy` 进程 10 次，下一轮驱动扫描必须成功 `--resume` 至少 9 次。

---

## 8. 后续演进方向

- **本地缓存镜像**：将 `reference/` 目录只读缓存到本地向量库，使 Hy3 无需联网即可查阅 musl 源码。
- **性能回归门禁**：在里程碑审查时，自动运行 `meow bench` 对比合并前后的编译速度，若性能下降 > 5%，自动驳回。
