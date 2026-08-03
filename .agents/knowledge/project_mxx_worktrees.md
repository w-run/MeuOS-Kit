---
name: mxx 团队 worktree 布局
description: mcc/m++ 重构团队的 worktree 分工，避免并发冲突
type: project
---

mcc/m++ 统一重构团队（mxx-dev-62bb）的 worktree 分工：

- `.agents/worktrees/mxx-work`（分支 worktree-mxx-work）：共享 worktree，general-purpose-4 在此做 m++ 虚表/模板（任务 #7），含未提交在途修改。此目录的 `make m++` 可能因在途代码链接失败。
- `.agents/worktrees/mxx-c2fix`（分支 worktree-mxx-c2fix）：我（general-purpose-3）为任务 #8 创建，基于 49d2b86，已推送 origin。已提交 42b30b1（C.2 七项限制修复）。
- 主仓库 `projects/` 下有 meuos-sysroot/meuos-toolchain 构建产物目录；`sysroot/x86_64/usr/lib/` 是主仓库 sysroot（多架构子目录布局）。

**Why:** 2026-08-02 发现两个 subagent 在同一 mxx-work worktree 并发编辑 cpp_parse.c 导致冲突和构建失败。

**How to apply:** 做 mcc/m++ 前端工作前先确认 worktree；避免在 mxx-work 里跑 `make m++`（会撞上虚表在途修改）。任务 #13（B.5 C 前端迁入 src/c/）明确要求等各 subagent 收敛后再做。

**并发经验（2026-08-02 constexpr 会话）**：共享 worktree 里其他 worker 的 git 操作（stash pop/checkout）会把「我已在工作区恢复/编辑的文件」整组原子还原（同一时刻 mtime）。对策：①从 stash 恢复文件后立即 `cp` 备份到 /tmp；②改动完成并自测通过后尽快提交（提交进 git 历史后无法被工作区操作抹掉）；③提交前 `git fetch` + `git pull --no-rebase` 同步（rebase 会因未暂存改动拒绝）；④commit 时只 `git add` 自己的文件，绝不 `git add -A`。mxx-work 的 make 默认目标只建 mcc 不建 m++，改 cpp_parse.c 等前端文件后需显式 `make m++`。

**共享 stash ref 串台事件（2026-08-03 mcc-team-r5 会话）**：repo 的 `refs/stash` 被所有 worktree 共享，`git stash push/pop` 会 push/pop **全局栈顶**，与当前 worktree 无关。grace 在 /tmp/mxx-wt-grace 连续 stash push+pop 时把 hazel 的 stash 弹出应用到自己 worktree（UU 冲突），随后又用 `git restore --source=HEAD` 覆盖时连带丢过自己的在途改动。教训：**共享 repo 的 worktree 严禁用 `git stash`（改用自己的临时分支 commit 或唯一 stash ref）**；用 `git checkout HEAD~1 -- <file>` 建基线二进制时，事后必须用 `git checkout HEAD -- <file>` 恢复的是"HEAD 提交内容"而非"恢复前的在途内容"——若 HEAD 不含最新改动，在途改动会被覆盖丢失。恢复他人被误弹的 stash：`git update-ref refs/stash <WIP commit>`（fsck --unreachable 可找 dangling stash commit）。**此纪律已由 team-lead 正式记入 worker-deployment.md §5**（2026-08-03，mcc-team-r5 裁决后）；团队后续会话以此文档为权威依据。

**验证竞态经验（2026-08-02 worker-test 会话）**：verify-all 在 worker-cpp 并发 `make mcc` 时跑会误报：①check-mir-bridge 被调试输出污染（当时 slotmerge.c:67 无条件 fprintf 混入 .s）；②check-sysroot-static 的 `cp -a` 复制到 worker 编辑中的半成品源码，或 HOST_CC mcc 二进制瞬时缺失（自举失败）；③`libmcc.a` 被并发 `ar` 写坏（file format not recognized）。对策：串行重跑（先确认无 make 进程）排除竞态；失败项用隔离复现判定真伪。

**重要更正（2026-08-02 晚）**：check-sysroot-static 的失败最初被误判为"并发竞态/既有环境问题"，但隔离复现证明是 **slotmerge 自举误编译（缺陷 J）**：f22f3d7 干净树串行下，自举 mcc（mcc 编译 mcc）编译 hello.c SIGSEGV，注释 `P(slotmerge)` 后正常。教训：**串行复现出的失败必须是真问题，不能用"竞态"带过；自举（mcc 编 mcc）是验证后端 pass 正确性的最强门禁**，check-c99/c11 小测试覆盖不到大函数误编译。worker-test 实测 slotmerge（f22f3d7）BZ2_decompress 帧 3976→2152B（-46%），与提交文档 -45% 一致。

**range-for 并发事件（2026-08-03 worker-cpp20 会话）**：team-lead 批准我实现 range-for 后，发现共享 worktree stmt.c 出现他人正在写的 +334 行未提交实现（`git status` 从干净突变）。我上报 team-lead 仲裁 + 用 `/tmp/mcciso` 隔离副本（rsync/cp + 软链 ../meuos-sysroot）独立测试，不改共享文件。最终并发 worker 提交 71fbb35（m++: C++ range-based for loops），两路独立定位出相同 4 处 bug（①非 range-for 路径 tokpush 后未压回 body token，破坏所有 C++ for；②range 类型解析同问题；③重写双 auto `auto __b=X, auto __e=Y` 非法；④body 后 token 被吞）。**教训：tokpush 压回时必须把当前 tok 一并压回，且 re-queue token 必须堆分配（xmalloc）**——栈拷贝传给 tokpush（ctxpush 存指针）会悬垂，崩 template.cc（tokenstr 断言）。隔离副本是安全并发测试的关键手段（构建需 `-L../meuos-sysroot/build -lmsys` 软链）。

**隔离副本路径约定（2026-08-03 mcc-team-r3 会话）**：`/tmp/mcciso` 是**共享危险路径**——多 worker 同时 `rm -rf /tmp/mcciso && cp -r ...` 会互相清空/平铺覆盖（cp -r 到不存在的 dst 会把 dst 直接变成 src 副本，破坏嵌套结构），验证结果不可信。**约定：每个 worker 用 `/tmp/mcciso-<worker名>` 个人路径，禁用 /tmp/mcciso**。标准建法：`git archive <commit> projects/mcc | tar -x` 到个人目录 → `mv projects/mcc mcc` → `sysroot/meuos-libc/meuos-sysroot` 用实体副本（cp -a，防止 check-sysroot-static install 污染共享目录）→ `meuos-toolchain/meuos-buildtools/meuos-compress/mxx` 用软链 → `env MEUOS_SYSROOT=<个人>/sysroot make <目标>`。
