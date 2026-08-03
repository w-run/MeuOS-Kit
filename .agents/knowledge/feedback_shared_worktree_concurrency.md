---
name: 共享 worktree 并发 git 操作教训
description: 在 mxx-work 共享 worktree 上做 reset/stash 会与并发进程交错导致状态混乱，需先确认再操作
type: feedback
---

在 team 共享 worktree（如 /workspace/MeuOS-Kit/.agents/worktrees/mxx-work）上执行 git 操作（stash push、reset --mixed、merge）时，可能与其他 worker 的并发操作交错，产生看似"自己执行了但 reflog 显示另有进程"的状态：我 2026-08-03 执行 `git stash push -- Makefile` + `git reset --mixed c44315e` 后，merge hazel 与 Makefile commit 被并发进程完成并 push（reflog 显示 69a6f2c/d0a90c9 由外部创建），我的 stash push 未在 reflog 注册，Makefile 改动最终作为 d0a90c9 提交。

**Why:** 该 worktree 被 team-lead/hazel 等 worker"积极管理"（diana 曾提示），git 的 refs/stash、HEAD、reflog 是共享资源，多进程并发写会交错。

**How to apply:**
- 在共享 worktree 做 reset/stash/checkout 前，先 `ps aux | grep git` + `git reflog -5` 确认无并发进程在操作。
- 若发现"自己没执行过但出现 commit/merge"，先核实提交内容是否等于自己预期成果（hash 校验 + git show），不要盲目重做或覆盖。
- 变更工作成果优先落到独立分支（如 worktree-tmp-*）再让 team-lead merge，避免在共享 worktree 上直接 reset 历史。
- 执行 `git stash push -- <path>` 后若担心丢失，立即 `git stash show` 确认内容，且把关键工作成果先落到文件级 commit（`git commit --only`）而非仅依赖 stash。
