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

## 决策逻辑（query.sh v4 内置，渠道策略 2026-08-04）

按**渠道策略优先级**决策（大喵 2026-08-04 定位）：

| 渠道 | 定位 | 使用策略 |
|------|------|---------|
| **MiniMax M3** | 只有周/5h 限制，间隔重置快 | 周配额充足时**可手动把某 variant(lite/reasoning) 临时改 variantModels 指向 custom-local:MiniMax-M3 猛用**；不自动走 default |
| **lite (DS flash)** | 经济实惠好吃不贵 | **性价比首选，常规任务主力** |
| **reasoning (GLM-5.2/Ark)** | **月总量告急**（monthly 15%） | **必须省着用**，仅必要复杂分析；monthly<20% 时禁用 |
| **codebuddy 自带** | 查不到积分余额 | **只能应急** |

**⚠️ 铁律：创建 subagent 时禁用 default**。settings.json 中 `default` 是随便配置的（设置时只有 lite/reasoning 有区分意义），不可靠。子任务只允许 `lite` 或 `reasoning`。

判定：
- MiniMax：按**周配额**（<20% 才 low；间隔仅短期提示，重置快）——周配额充足时在 `minimax_note` 提示可手动切 M3
- DeepSeek：余额 <¥10 → low
- Ark：**按月总量**（monthly<20% → reasoning 禁用）

决策顺序：
1. **lite**（DS flash 性价比）为常规主力（default 禁用）
2. DeepSeek 余额 low → 转 `hy3` 免费
3. `reasoning_available`：Ark monthly≥20%（或走 local-free）→ yes，由指挥官按需启用
4. MiniMax 周配额充足 → `minimax_note` 提示可手动切 M3（需改 variantModels），非默认决策

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
