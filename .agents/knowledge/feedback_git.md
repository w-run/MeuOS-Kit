---
name: git 工作纪律与并发安全
description: 文件级 git add、共享 worktree 并发/禁 stash、主线快速推进判断、worktree 构建陈旧、main 合并即推送
type: feedback
---

# Git 工作纪律（多 worker/多 worktree 场景沉淀）

## 1. 提交纪律：文件级 git add + `git commit -- <path>`

多 worker 并发改同一 worktree 时，每个 worker 提交必须文件级 `git add <具体文件>`，**严禁 `git add -A`/目录级 add**。最稳妥姿势：直接用 `git commit --only <path>`（或 `git commit -- <path>`），完全绕过共享暂存区，不受他人并发 add/commit 影响。commit 后立即 push 并 `git show --name-status HEAD` 核对。

**Why:** 已发生三次夹带事故（647a05b 夹带 F、93ab4b4 夹带 V、6ca4ba1 夹带 pp.c），根因都是共享 git index——即使自己做了文件级 add，另一 worker 的全量 commit 仍可吞掉已 staged 文件。

**How to apply:** commit 前 `git status` 自查暂存区（M/A 列）+ `git diff --cached --name-only` 双保险；出现非自己文件先 `git restore --staged` 归还。禁止 `git reset --hard`/`git checkout .`/`git clean -f`（丢弃他人改动）。遇已 push 的夹带：**维持现状不 force push**（重写祖先链破坏已 pull 的 worker 树），文档记录即可。共享 stash ref 严禁乱用（见 §3）。

## 2. 共享 worktree 并发 git 操作

在共享 worktree 执行 stash/reset/checkout/merge 前，先 `ps aux | grep git` + `git reflog -5` 确认无并发进程。发现"自己没执行过但出现 commit/merge"，先核实提交内容等于自己预期成果（hash + git show），不盲目重做或覆盖。变更工作成果优先落独立分支再 merge。

## 3. 共享 stash ref 串台

repo 的 `refs/stash` 全局共享，stash push/pop 操作**全局栈顶**，与当前 worktree 无关。**共享 repo 的 worktree 严禁用 `git stash`**（改用临时分支 commit 或唯一 stash ref）。恢复被误弹的 stash：`git update-ref refs/stash <WIP commit>`（fsck --unreachable 找 dangling）。

## 4. 主线快速推进下的判断纪律

遇到"异常"（门禁变红、成果被覆盖、worktree 消失），判断次序：**先 `git fetch` 对齐 HEAD → 再确认改动是否真的不在树上 → 最后才怀疑回归**。主线程并发合并极快，曾单次 fetch 间隔连推 4 笔。三次教训都是"拿过期 HEAD 快照解释当下现象"。验证成果存活用 `git merge-base --is-ancestor <commit> origin/<主线>` + `git show HEAD:<path> | grep` 点名核对。测试与配套修复应同 commit 归并（根除"测试先到修复后到"假红）。

## 5. worktree 构建陈旧

worktree 跨分支切换后 `build/` 保留，部分 .o mtime 比源码新 → make 不重编用旧二进制。**改 src/** 后必须 `make clean && make`**，别信增量构建；排查"改了源码但行为没变"优先怀疑陈旧 .o。

## 6. main 分支合并即推送

main 一旦有合并/提交立即 `git push origin main`，无需再确认。`daily-audit` 分支永不合并 main（仅定期 merge main -> daily-audit 同步）。

## 7. verify-all 并发竞态误报

verify-all 在并发 make 时跑会误报：check-mir-bridge 被调试输出污染、check-sysroot-static `cp -a` 复制半成品、`libmcc.a` 被并发 ar 写坏。**对策**：串行重跑（先确认无 make 进程）；失败项隔离复现判定真伪。**注意**：串行复现出的失败必须是真问题，不能用"竞态"带过；自举（mcc 编 mcc）是验证后端 pass 正确性的最强门禁。

## 8. 隔离副本路径

`/tmp/mcciso` 是共享危险路径，多 worker 同时 rm -rf 会互相清空。每个 worker 用 `/tmp/mcciso-<worker名>`。建法：`git archive <commit> projects/mcc | tar -x` → `mv projects/mcc mcc` → sysroot 实体副本（cp -a）→ 其余软链 → `env MEUOS_SYSROOT=<个人>/sysroot make <目标>`。
