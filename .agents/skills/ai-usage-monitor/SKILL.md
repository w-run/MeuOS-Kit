---
name: ai-usage-monitor
description: 查询 AI 渠道用量并决策模型使用方案。当需要检查模型用量、决策 spawn worker 用哪个模型变体、定时监控 AI 余额/额度时使用。
---

# ai-usage-monitor

查询三个 AI 渠道的实时用量，并给出模型使用决策建议。供指挥官在 spawn worker / 续接会话前快速评估该用什么模型变体。

## 数据源（复用 /root/ai.sh 的底层脚本）

| 渠道 | 命令 | 语义 |
|------|------|------|
| MiniMax Token Plan | `MINIMAX_API_KEY=... /root/.codebuddy/minimax-usage.sh` | 输出 `0:78:ts1:ts2` = `间隔剩余%:周剩余%:间隔重置epoch:周重置epoch`（**剩余%**） |
| DeepSeek 按量 | `/root/.codebuddy/deepseek-balance.sh` | 输出余额 `¥`（34.69） |
| Ark 方舟 Coding Plan | `arkcli usage plan --format json` | percent 为**已用%**，需 `100 - used` 得剩余%；另含 `reset_at` 重置时间 |

## 运行

```bash
# 单次快照 + 决策（推荐，输出 JSON 便于解析）
bash /workspace/MeuOS-Kit/.codebuddy/skills/ai-usage-monitor/query.sh

# 交互式查看完整用量（TUI，每 10s 刷新，Ctrl+C 退出）
/root/ai.sh
```

## 决策逻辑（query.sh 内置）

简化策略——**优先 lite，reasoning 按需且仅在 Ark 充足时启用**：

1. **默认 `lite`**：免费、可并行、命中率高。除极少数复杂根因/架构分析外一律 lite。
2. **Ark session/weekly 任一 <20%**：`reasoning_available=no`，禁止 reasoning（GLM-5.2 走 Ark Coding Plan）。
3. **MiniMax 间隔 <20%**：禁用 default/高耗模型，全部走 lite 或按量付费。
4. **reasoning**：仅当 Ark session ≥30% 时 `reasoning_available=yes`，且由指挥官显式按需启用（同时最多 1-2 个）。

## 指挥官使用规范

- spawn worker 时 `model` 参数**必须显式**为 `lite` 或 `reasoning`，**禁用 default**；
- spawn 前先跑 `query.sh` 看 `decision.suggested` 与 `reasoning_available`；
- 若 MiniMax plan 达上限（429），切会话主模型到 codebuddy 自带 deepseek-v4-flash（按量）规避，worker 仍用 lite；
- 定期（如 /loop）自动跑 query.sh，决策模型方案后再批量 spawn。

## 模型变体映射（settings.json variantModels 决定）

| 变体 | 实际模型 | 渠道 | 资源特性 |
|------|---------|------|---------|
| `lite` | DeepSeek-V4-Flash | 项目/按量 | 免费、可并行、命中率高 |
| `reasoning` | GLM-5.2 | 火山方舟 Coding Plan | 贵、稀缺（同时 ≤2），仅复杂任务 |
| `default` | 会话主模型 | 视当前 | **禁用**，除非指挥官显式启用 |
