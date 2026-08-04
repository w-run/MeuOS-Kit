---
name: ai-usage-monitor
description: 查询 AI 渠道用量并决策模型使用方案。当需要检查模型用量、决策 spawn worker 用哪个模型变体、定时监控 AI 余额/额度时使用。
---

# ai-usage-monitor

查询各 AI 渠道的实时用量，并结合 `settings.json` 的 `variantModels` 与 `models.json` 的真实模型路由，给出模型使用决策建议。供指挥官在 spawn worker / 续接会话前评估该用什么模型变体。

## 关键：模型路由（custom-local: 前缀含义）

`settings.json` 的 `variantModels` 用 `custom-local:<id>` 形式。`custom-local:` 表示这是**本地注册模型**，由 `models.json` 的 `url/apiKey` 映射到真实厂商端点计费：

| variantModels 值 | models.json 真实 id | 真实端点 / 计费渠道 |
|------------------|--------------------|--------------------|
| `custom-local:deepseek-v4-flash` | `deepseek-v4-flash` | `api.deepseek.com` → **DeepSeek 官方按量** |
| `custom-local:glm-5.2` | `glm-5.2` | `ark.cn-beijing.volces.com/api/coding` → **Ark Coding Plan** |
| `custom-local:MiniMax-M3` | `MiniMax-M3` | `api.minimaxi.com` → **MiniMax Token Plan** |
| `custom-local:catpaw/glm-5.2` | `catpaw/glm-5.2` | `127.0.0.1:8046` → **本地免费** |
| `hy3` | — | 免费变体 |

当前 `variantModels`（2026-08-04）：
```
lite      = custom-local:deepseek-v4-flash   → DeepSeek 按量
default   = custom-local:deepseek-v4-flash   → DeepSeek 按量（禁用）
reasoning = custom-local:glm-5.2             → Ark Coding Plan
```

**注意**：`lite` 并非完全免费——它走 DeepSeek 官方按量，消耗 `deepseek-balance.sh` 余额。真正免费的是 `hy3` 与 `catpaw/glm-5.2`。

## 数据源

| 渠道 | 命令 | 语义 |
|------|------|------|
| MiniMax Token Plan | `MINIMAX_API_KEY=... /root/.codebuddy/minimax-usage.sh` | `0:78:ts1:ts2` = `间隔剩余%:周剩余%:重置epoch`（**剩余%**） |
| DeepSeek 按量 | `/root/.codebuddy/deepseek-balance.sh` | 余额 `¥`（34.69） |
| Ark Coding Plan | `arkcli usage plan --format json` | percent 为**已用%**（100-used 得剩余%），另有 reset_at |
| variantModels / 路由 | `/root/.codebuddy/settings.json` + `/root/.codebuddy/models.json` | 变体→真实模型→计费渠道 |

## 运行

```bash
bash /workspace/MeuOS-Kit/.codebuddy/skills/ai-usage-monitor/query.sh   # JSON 快照 + 决策
/root/ai.sh                                                             # 交互式 TUI（每 10s）
```

## 决策逻辑（query.sh v2 内置）

按**各 variant 的实际计费渠道余量**评估可用性，再决策：

1. 解析 `settings.json` → 每个 variant（lite/default/reasoning）映射到真实 channel（deepseek/ark/minimax/local-free）。
2. 采集三渠道用量：MiniMax 间隔%、DeepSeek 余额、Ark session/weekly%。
3. 可用性判定（<20% 视为 low）：
   - DeepSeek 余额 <¥10 → low
   - Ark session 剩余 <20% → low
   - MiniMax 间隔 <20% → low
4. 决策：
   - 默认 **lite**（DeepSeek 按量）。
   - DeepSeek 余额 low → 建议 **hy3**（免费）。
   - `reasoning_available`：仅当 reasoning 走 Ark 且 session ≥20%（或走 local-free）时为 yes；由指挥官按需启用（同时 ≤2）。

## 指挥官使用规范

- spawn worker 时 `model` 参数**必须显式**为 `lite` 或 `reasoning`，**禁用 default**；
- spawn 前先跑 `query.sh` 看 `decision.suggested`、`reasoning_available` 与各 channel ok 状态；
- MiniMax plan 达上限（429）时，切会话主模型到 codebuddy 自带 deepseek-v4-flash（按量）规避，worker 仍用 lite；
- 定期（/loop）跑 query.sh 决策模型方案后再批量 spawn；
- 需要复杂根因/架构分析时，确认 `reasoning_available=yes` 再启用 reasoning。

## 模型变体映射（settings.json variantModels + models.json）

| 变体 | 实际模型 | 计费渠道 | 资源特性 |
|------|---------|---------|---------|
| `lite` | deepseek-v4-flash | DeepSeek 按量 | 常规，命中率高 |
| `reasoning` | glm-5.2 | Ark Coding Plan | 贵、稀缺（同时 ≤2），仅复杂任务 |
| `default` | deepseek-v4-flash | DeepSeek 按量 | **禁用**，除非指挥官显式启用 |
| `hy3` | — | 免费 | 简单/重复/可并行 |
| `catpaw/glm-5.2` | — | 本地免费 | 本地推理 |
