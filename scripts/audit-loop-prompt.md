# 每日代码校验与缺陷检查 — CodeBuddy `/loop` 驱动说明

本文件供 **CodeBuddy（当前 agent）的 `/loop`** 使用：脚本 `scripts/daily-audit.sh`
负责数据驱动的基础核查（构建/测试、文档对照、桩扫描、git 记录），而 **Agent 自身**
负责「全量 AI 复核」——即智能缺陷检查与实现偏差分析。两者配合：脚本采数，
Agent 读数与代码后补全深度结论。

## 分支模型（B：严格隔离）

- `daily-audit`：常驻工作目录（含本脚本与提示词），推送远端、**永不合并到 main**；
  定期 `git merge main` 保持同步。Agent 的 `/loop` 会话应工作在该分支上。
- `daily-<MMDD>`：每日结果分支，**基于 main** 创建，仅含 `.issues/<MMDD>.md` 与修复；
  直接 PR 到 main。报告与修复同支，发现缺陷时**修复直接提交到该分支**。

## 用法

在本仓库、且位于 `daily-audit` 分支的 CodeBuddy Code 会话中执行（推荐在「本地凌晨」启动，
使每日循环对齐到凌晨）：

```
/loop 24h 见 scripts/audit-loop-prompt.md 中的 LOOP_PROMPT
```

或直接把下方 `LOOP_PROMPT` 的内容作为 `/loop 24h` 的参数粘贴。

> 说明：`/loop` 的间隔为「自启动起」的相对间隔（如 `24h`）。要锚定到「每天凌晨」，
> 请在你的本地凌晨时刻启动该 loop；若需严格 00:00 UTC 定点，可用
> `.github/workflows/daily-audit.yml`（GitHub Actions，已在 main 上）作为备用触发器。

## LOOP_PROMPT

```
每日代码校验与缺陷检查（由 CodeBuddy loop 驱动，分支模型 B）：
0. 确认当前在 daily-audit 分支；若不在，`git checkout daily-audit && git pull --ff-only origin daily-audit`。
1. 运行 `date -u +%m%d` 得到今日四位数日期，记为 DATE。若 `origin/daily-<DATE>` 分支已存在，或本地 `.issues/<DATE>.md` 已存在，输出"今日已审计"并结束本次循环。
2. 在 daily-audit 上运行 `bash scripts/daily-audit.sh --date <DATE> --agent` 生成数据驱动的基础报告（含构建/测试、文档对照、桩扫描、git 记录），末尾留 `<!--AGENT-REVIEW-->` 占位。
3. 基于以下资料做「全量 AI 复核」，只新增发现、不改动其他文件：
   - 刚生成的 `.issues/<DATE>.md`
   - `.issues/` 下历史审计（重点看 P0/P1/P2 状态与进度结论）
   - 各子项目 `ARCHITECTURE.md`（设计意图）
   - `git log`（自上一期审计以来的提交）
   重点发现（不止桩标记）：
   - 真实缺陷：越界/空指针/未初始化/并发/资源泄漏/错误码未处理/UB 等；
   - 实现与文档或预期意图的偏差（对照 .issues 中的目标与 ARCHITECTURE 设计）；
   - 自上一期以来新引入的回归或风险点。
   每条发现给出 `file:line` 证据、严重度（P0/P1/P2）、建议修复。
4. 用 Edit 工具把报告中的 `<!--AGENT-REVIEW-->` 占位替换为「## 六、Agent 深度复核」章节（含发现表格 + 综合结论），并补上页脚说明。
5. 把报告搬到「基于 main」的结果分支：
   - `cp .issues/<DATE>.md /tmp/daily-report-<DATE>.md`
   - `git checkout -B daily-<DATE> origin/main`
   - `mkdir -p .issues && cp /tmp/daily-report-<DATE>.md .issues/<DATE>.md`
   - `git add .issues/<DATE>.md`
6. 修复：基于第 3 步发现，直接在 daily-<DATE> 上修改对应源码文件，并 `git add` 这些文件。
7. 提交并推送：`git commit -m "audit: 每日代码校验与缺陷检查 <DATE>" && git push -u origin daily-<DATE>`。
8. 若 `gh` 可用且同分支 PR 不存在，用 `gh pr create` 开 PR 到 `main`（标题 `audit: 每日代码校验与缺陷检查 <DATE>`，正文指向本报告与修复说明）。
全程使用简体中文；不要改动 daily-audit 分支本身（保持其永不合并 main）；除报告与修复外不要提交无关文件。
```

## 与 GitHub Action 的关系

- `.github/workflows/daily-audit.yml`（UTC 00:00 定点，已置于 main）与 `/loop` 二者**幂等兼容**：
  `daily-<DATE>` 分支或报告一旦存在，另一方自动跳过，不会互相覆盖。
- 该 workflow 通过 `ref: daily-audit` 取用工作目录中的脚本，并把报告搬到基于 main 的
  `daily-<DATE>` 分支提交（PR 仅含报告）；Agent 驱动的修复则直接落到同一 `daily-<DATE>` 分支。
- 推荐：以 `/loop` 为主驱动（带 AI 深度复核）；workflow 作为「严格定点凌晨」的备用触发器。
  若希望每日必带 AI 复核，可禁用该 workflow（Settings → Actions）。
