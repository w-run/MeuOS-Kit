# 每日代码校验与缺陷检查 — CodeBuddy `/loop` 驱动说明

本文件供 **CodeBuddy（当前 agent）的 `/loop`** 使用：脚本 `scripts/daily-audit.sh`
负责数据驱动的基础核查（构建/测试、文档对照、桩扫描、git 记录），而 **Agent 自身**
负责「全量 AI 复核」——即智能缺陷检查与实现偏差分析。两者配合：脚本采数，
Agent 读数与代码后补全深度结论。同一日报告可**查缺补漏式多次追加**，并在文末「修订记录」中留痕。

## 分支模型（B：严格隔离）

- `daily-audit`：常驻工作目录（含本脚本与提示词），推送远端、**永不合并到 main**；
  定期 `git merge main` 保持同步。Agent 的 `/loop` 会话应工作在该分支上。
- `daily-<MMDD>`：每日结果分支，**基于 main** 创建，仅含 `.issues/<MMDD>.md` 与修复；
  直接 PR 到 main。报告与修复同支，发现缺陷时**修复直接提交到该分支**。

## 用法

在本仓库、且位于 `daily-audit` 分支的 CodeBuddy Code 会话中执行。触发时点为**北京时间
02:00 / 08:00 / 14:00 / 20:00（每天 4 次）**，与 GitHub Action 对齐。

**方式 A — 本地会话用 CronCreate / `/loop` 锚定到本地时刻（推荐，带 AI 深度复核）：**

```
/loop 见 scripts/audit-loop-prompt.md 中的 LOOP_PROMPT
```

`/loop` 只能按本地时区触发；把会话启动在本地凌晨、或直接用 `CronCreate` 锚定到
`0 2,8,14,20 * * *`（本地时区，对应北京时间 2/8/14/20）即可每天跑 4 次。

> 注：`CronCreate` 任务是**会话级**的，会话退出即失效，且 3 天后自动过期 —— 重启 CodeBuddy
> 会话后需用 `/loop` 或 `CronCreate` 重新建立该循环（见下方「与 GitHub Action 的关系」）。

**方式 B — 直接粘贴 `LOOP_PROMPT`** 作为 `/loop` 的参数，二者等价。

## LOOP_PROMPT

```
每日代码校验与缺陷检查（CodeBuddy loop 驱动；分支模型 B；支持每日多次运行/查缺补漏 + 修订记录）：
0. 确认在 daily-audit；若不在，`git checkout daily-audit && git pull --ff-only origin daily-audit`。`git fetch origin`。
1. DATE=$(date -u +%m%d)。`HAS_REMOTE=$(git ls-remote --heads origin daily-<DATE> | grep -q . && echo yes || echo no)`。
2. 判定首跑还是查缺补漏（每天可多次运行，弥补单次 agent 的遗漏/误判）：
   - 若本地 `.issues/<DATE>.md` 不存在 且 HAS_REMOTE!=yes → 首跑：
     a. `bash scripts/daily-audit.sh --date <DATE> --agent` 生成基础报告（含 <!--AGENT-REVIEW--> 占位与「七、补充发现」「修订记录」空表）。
     b. 读报告+历史.issues+各子项目 ARCHITECTURE.md+git log，用 Edit 把 <!--AGENT-REVIEW--> 替换为「## 六、Agent 深度复核」（发现表格：file:line、P0/P1/P2、建议修复 + 综合结论）。
     c. 在「七、补充发现」追加初始要点；在「修订记录」追加一行 `| <时间> | Agent | 初始复核：<摘要> |`。
   - 否则（已存在，查缺补漏）：
     a. 同步远端最新报告到工作树（保证跨会话/多次运行累积）：`git show origin/daily-<DATE>:.issues/<DATE>.md > .issues/<DATE>.md 2>/dev/null || true`（仅当 HAS_REMOTE=yes）。
     b. `bash scripts/daily-audit.sh --date <DATE> --append`：脚本轻量刷新缺陷扫描/git 区间，在「七、补充发现」追加一段并自动加修订记录行（不重跑构建）。
     c. 读现有报告与新数据，用 Edit 在「六、Agent 深度复核」或「七、补充发现」追加新发现（file:line、P0/P1/P2、建议修复）；未填的 <!--AGENT-REVIEW--> 占位也在此补上。
     d. 在「修订记录」追加一行 `| <时间> | Agent 查缺补漏 | <本次补充摘要> |`。
3. 提交与推送（daily-<DATE> 基于 origin/daily-<DATE>，缺失则基于 origin/main，保证多次运行可快进推送）：
   - `cp .issues/<DATE>.md /tmp/daily-report-<DATE>.md`
   - `git checkout -B daily-<DATE> origin/daily-<DATE> 2>/dev/null || git checkout -B daily-<DATE> origin/main`
   - `mkdir -p .issues && cp /tmp/daily-report-<DATE>.md .issues/<DATE>.md`
   - `git add .issues/<DATE>.md`
   - `git commit -m "audit: 每日代码校验与缺陷检查 <DATE>" && git push -u origin daily-<DATE>`
4. 若 `gh` 可用且 PR 不存在，开 PR 到 main（标题同上，正文指向报告与修复说明）。
重点发现（不止桩标记）：真实缺陷（越界/空指针/未初始化/并发/资源泄漏/错误码未处理/UB）、实现与文档或预期意图的偏差、自上一期以来新引入的回归或风险点。
全程简体中文；不要改动 daily-audit 分支本身（永不合并 main）；除报告与修复外不提交无关文件。
```

## 与 GitHub Action 的关系

- `.github/workflows/daily-audit.yml`（已置于 main，cron 为 UTC，已换算为北京时间 2/8/14/20
  对应的 `0 18/0/6/12 * * *`）与 `/loop` 二者**幂等兼容**：
  `daily-<DATE>` 分支或报告一旦存在，另一方自动跳过，不会互相覆盖。
- 该 workflow 通过 `ref: daily-audit` 取用工作目录中的脚本，并把报告搬到基于 main 的
  `daily-<DATE>` 分支提交（PR 仅含报告）；Agent 驱动的修复则直接落到同一 `daily-<DATE>` 分支。
- 推荐：以 `/loop`（本地会话）为主驱动（带 AI 深度复核），每天 4 次查缺补漏；workflow 作为
  「严格定点」的备用触发器（CI 环境、或无本地会话时也能跑）。两者都先同步远端报告再追加，
  互不覆盖。若希望每日必带 AI 复核，可禁用该 workflow（Settings → Actions）。
