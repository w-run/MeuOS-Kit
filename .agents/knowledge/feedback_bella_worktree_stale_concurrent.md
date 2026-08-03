---
name: bella worktree 构建陈旧与并发写入
description: /tmp/mxx-wt-bella 的 build/ 跨分支切换会陈旧导致假失败；2026-08-03 出现非本会话的并发写入
type: feedback
---

**规则 1：/tmp/mxx-wt-bella（及任何跨分支切换的 worktree）改动源码后必须 `make clean && make`，否则是假结果。**

**Why:** 该 worktree 的 `build/` 目录跨 git checkout 分支切换而保留；切换后部分 .o 的 mtime 可能比源码新，`make` 不重编 → 用了旧分支的二进制。本次会话中多次被误导：
- 起初"≤16B 聚合返回 rc=139"实为 63411d8 源码已含 e4a885c pad 修复但二进制陈旧；
- 修完后"chain rc=1"又是构建陈旧（restore 后未 clean），`make clean` 后 rc=0。

**How to apply:** 在这个 worktree 里每次改 `src/**` 后先 `make clean` 再 `make mcc m++`，别信增量构建；排查"改了源码但行为没变"优先怀疑陈旧 .o。

---

**规则 2：/tmp/mxx-wt-bella 存在并发写入（2026-08-03 实测）。**

**Why:** 本会话修复 ≤16B 聚合返回期间，工作树出现非本会话产生的改动：`src/mir/regalloc.c`（multi-def 区间修复）、`test/c11/aggregate_return.c`、`test/cpp/aggregate_return.cc`；且 regalloc.c 在我 commit 后（16:34）仍被外部改写 mtime。改动与我的修复方向完全一致，疑似并行 bella/chloe 会话操作同一 worktree（团队曾多次重建，可能产生重复 worker）。

**How to apply:** 在本 worktree 操作前 `git status` 先看有没有非预期改动；若出现外部写入，先验证其正确性再决定并入或报告 team-lead，不要覆盖或丢弃；并向 team-lead 汇报并发隐患。
