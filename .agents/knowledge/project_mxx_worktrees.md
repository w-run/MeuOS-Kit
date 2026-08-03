---
name: mcc 工作树布局与并发经验
description: mcc/m++ 工作树布局（mcc-toolchain）与共享 worktree 并发验证的踩坑经验
type: project
---

mcc/m++ 工作树与并发经验（2026-08-04 更新：mxx 代号废弃，分支已重命名）：

**当前布局**：
- `.agents/worktrees/mcc-toolchain`（分支 `mcc-toolchain`）：mcc/m++ 主线工作树（原 mxx-work）。
- `projects/mcc/`：编译器主目录；`make` 默认只建 mcc，改 cpp_parse.c 等 C++ 前端后需显式 `make m++`。
- `sysroot/x86_64/usr/lib/`：主仓库 sysroot（多架构子目录布局）。

**Why（初始动机）**: 2026-08-02 两个 subagent 在同一 worktree 并发编辑 cpp_parse.c 导致冲突和构建失败。

**共享 worktree 并发纪律**（详见 feedback_shared_worktree_concurrency.md）：
- 共享 repo 的 `refs/stash` 全局共享：stash push/pop 操作**全局栈顶**，与当前 worktree 无关，严禁在共享 worktree 用 stash。
- reset/stash/checkout 前先 `ps aux | grep git` + `git reflog -5` 确认无并发进程。
- 关键工作成果落到文件级 commit（`git commit --only`），不要只依赖 stash。
- 恢复被误弹的 stash：`git update-ref refs/stash <WIP commit>`（fsck --unreachable 找 dangling）。

**验证竞态经验（verify-all 在并发 make 时会误报）**：
- check-mir-bridge 被调试输出污染；check-sysroot-static 的 `cp -a` 复制到半成品源码；`libmcc.a` 被并发 `ar` 写坏。
- **对策**：串行重跑（先确认无 make 进程）排除竞态；失败项用隔离复现判定真伪。
- **重要**：串行复现出的失败必须是真问题，不能用"竞态"带过。自举（mcc 编 mcc）是验证后端 pass 正确性的最强门禁。

**隔离副本路径约定**：`/tmp/mcciso` 是共享危险路径，多 worker 同时 rm -rf 会互相清空。每个 worker 用 `/tmp/mcciso-<worker名>` 个人路径。标准建法：`git archive <commit> projects/mcc | tar -x` → `mv projects/mcc mcc` → sysroot 用实体副本（cp -a）→ 其余用软链 → `env MEUOS_SYSROOT=<个人>/sysroot make <目标>`。
