---
name: mxx-work worktree 残留 rebase 无损清理（2026-08-04）
description: mcc-toolchain worktree（git-dir=mxx-work）残留他人 rebase，验收与清理纪律；lead-doc-mir-baseline 实为 m++/MIR 主线
type: project
---

# mxx-work 残留 rebase 清理 + lead-doc-mir-baseline 主线认知

## 背景

- mcc-toolchain worktree（路径 `/workspace/MeuOS-Kit/.agents/worktrees/mcc-toolchain`，git-dir=`/workspace/MeuOS-Kit/.git/worktrees/mxx-work`）曾残留他人对 `tmp/lead-doc-mir-baseline` 的 **m++/MIR 重写 rebase**（onto=`3163dac`，当时状态 done 20 + 待应用 545）。
- **mxx 代号已废弃**，后续一律用 `mcc-toolchain` 名称引用该 worktree。

## 清理纪律（无损 rebase abort）

- `git rebase --abort` 是**无损**的：它只回滚 rebase 机制性状态，不动已提交内容。关键前提是**先确认所有待应用提交已保存在远端/本地分支**，再 abort。
- 清理前验证链：`git fetch origin` → 确认 `origin/tmp/lead-doc-mir-baseline` 存在且为最新基线（此例 `2d4b65a`）→ 确认本地分支无独有未推送提交 → 再 `git rebase --abort`。
- 绝对禁止 `git reset --hard` / `stash` / commit 弃用等手段粗暴清场（见 feedback_git.md 纪律）；残留 rebase 正确姿势是先确认远端分支兜底再 abort。
- 清理后 `git status -sb` 该返回正常 `## tmp/lead-doc-mir-baseline...origin/...`，无 `rebase in progress` 标记。

## lead-doc-mir-baseline：真实身份是 m++/MIR 主线（不只是 toolchain 聚合基线）

- `tmp/lead-doc-mir-baseline` 是 **m++/MIR 开发主线**（当前实测 1398 提交，HEAD=`ec980c8`，已并 `origin/tmp/exec-mcc/merge-4345`）：
  - 含 **LIR 桥接移除**（跨 mcc 前/中/后端 MIR 单通道）；
  - **mcombine pass**；
  - **cpp class / 继承 / 命名空间** 支持；
  - **m++ 双二进制骨架**。
- 因此操作该分支时要当作 m++ 编译器主线维护，不要误当作"toolchain 聚合基线"只做小改动；涉 m++/MIR 的跨组件改动仍走独立 worktree（`tmp/<agent>/<feature>`）再合入。
- 基线验收锚点（历史）：聚合 rtld-p0 + i386 PIC GOT + mt/ld 动态节区 + g_pic 修复于 `2d4b65a`；近期已再并入 mcc #43/#45（merge-4345 → `ec980c8`）。

## Why

- 残留 rebase 是并发协作的典型脏状态；错误清理（reset/stash）会丢他人 m++/MIR 未推送提交，且 `mxx-work` 命名混乱易造成误操作。
- 正确认知 dev 分支真实身份（m++ 主线），才能避免把主线当聚合基线随意改动。

## How to apply

- 遇 `rebase in progress`：先 `git fetch origin` 确认待应用提交已上远端分支兜底，再 `git rebase --abort`，绝不 reset/stash。
- mcc-toolchain worktree 就叫 `mcc-toolchain`，不再用 `mxx-work` 代号。
- 改动 `tmp/lead-doc-mir-baseline` 时按 m++/MIR 主线粒度处理。
