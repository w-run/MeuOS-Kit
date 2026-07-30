# 每日代码校验与缺陷检查 — CodeBuddy `/loop` 驱动说明

本文件供 **CodeBuddy（当前 agent）的 `/loop`** 使用：脚本 `scripts/daily-audit.sh`
负责数据驱动的基础核查（构建/测试、文档对照、桩扫描、git 记录），而 **Agent 自身**
负责「全量 AI 复核」——即你最初想要的智能缺陷检查与实现偏差分析。两者配合：脚本采数，
Agent 读数与代码后补全深度结论。

## 用法

在本仓库的 CodeBuddy Code 会话中执行（推荐在「本地凌晨」启动，使每日循环对齐到凌晨）：

```
/loop 24h 见 scripts/audit-loop-prompt.md 中的 LOOP_PROMPT
```

或直接把下方 `LOOP_PROMPT` 的内容作为 `/loop 24h` 的参数粘贴。

> 说明：`/loop` 的间隔为「自启动起」的相对间隔（如 `24h`）。要锚定到「每天凌晨」，
> 请在你的本地凌晨时刻启动该 loop；若需严格 00:00 UTC 定点，可保留
> `.github/workflows/daily-audit.yml` 作为备用触发器（见下「与 GitHub Action 的关系」）。

## LOOP_PROMPT

```
每日代码校验与缺陷检查（由 CodeBuddy loop 驱动）：
1. 运行 `date -u +%m%d` 得到今日四位数日期，记为 DATE。若 `.issues/<DATE>.md` 已存在，输出"今日已审计"并结束本次循环。
2. 分支：若 `audit/<DATE>` 不存在则 `git checkout -b audit/<DATE>`，否则 `git checkout audit/<DATE>`。
3. 运行 `bash scripts/daily-audit.sh --date <DATE> --agent` 生成数据驱动的基础报告（含构建/测试、文档对照、桩扫描、git 记录）。
4. 基于以下资料做「全量 AI 复核」，并只新增发现、不改动其他文件：
   - 刚生成的 `.issues/<DATE>.md`
   - `.issues/` 下历史审计（重点看 P0/P1/P2 状态与进度结论）
   - 各子项目 `ARCHITECTURE.md`（设计意图）
   - `git log`（自上一期审计以来的提交）
   重点发现（不止桩标记）：
   - 真实缺陷：越界/空指针/未初始化/并发/资源泄漏/错误码未处理/UB 等；
   - 实现与文档或预期意图的偏差（对照 .issues 中的目标与 ARCHITECTURE 设计）；
   - 自上一期以来新引入的回归或风险点。
   每条发现给出 `file:line` 证据、严重度（P0/P1/P2）、建议修复。
5. 用 Edit 工具把报告中的 `<!--AGENT-REVIEW-->` 占位替换为「## 六、Agent 深度复核」章节（含发现表格 + 综合结论），并补上页脚说明。
6. 提交并推送：`git add .issues/<DATE>.md && git commit -m "audit: 每日代码校验与缺陷检查 <DATE>" && git push -u origin audit/<DATE>`。
7. 若 `gh` 可用且同分支 PR 不存在，用 `gh pr create` 开 PR 到 `main`（标题 `audit: 每日代码校验与缺陷检查 <DATE>`，正文指向本报告）。
全程使用简体中文；不要改动 `.issues/<DATE>.md` 以外的文件。
```

## 与 GitHub Action 的关系

- `.github/workflows/daily-audit.yml`（UTC 00:00 定点）与 `/loop` 二者**幂等兼容**：
  报告文件或 `audit/<DATE>` 分支一旦存在，另一方会跳过，不会互相覆盖。
- 推荐：**以 `/loop` 为主驱动**（带 AI 深度复核）；GitHub Action 作为「严格定点凌晨」
  的备用触发器。若担心 Action 先跑导致当日缺少 AI 复核，可在仓库禁用该 workflow
  （Settings → Actions → 禁用 `Daily Code Audit`），仅保留 loop。
