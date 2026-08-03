---
name: main 分支合并即推送
description: 用户规定 main 分支只要有新合并/提交就推送到 origin（本仓库协作规则）
type: feedback
---

本仓库（MeuOS-Kit）中，`main` 分支一旦获得新的合并或提交，就立即 `git push origin main`，无需再次单独确认。

**Why:** 用户明确指示「main 分支只要有合并就推送到远程」。环境会把各 `worktree-*` 分支 fast-forward 合进本地 main，按此规则这些合并应直接上行。

**How to apply:** 在 main 上提交或合并后，直接推送 main 到 origin（属共享分支，但用户已授权此规则，不必每次询问）。与之相对：`daily-audit` 分支作为每日审计工作目录会推送到远端，但**永不合并到 main**（仅定期 `merge main -> daily-audit` 同步）。
