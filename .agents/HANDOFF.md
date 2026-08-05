# HANDOFF.md — mcc/m++ 团队会话交接（2026-08-03 末）

> 本文件是 `worker-deployment.md` 的精简快照，供下一会话 5 分钟接手。
> 权威细节见 `.agents/worker-deployment.md`（§3 及其子节）。
> ⚠️ 本文档是 r6→r7 过渡期的历史快照。r7 团队已完成（T01-T19 全合入），
> 其中 lite/hy3 模型纪律在当时有效，hy3 免费额度已于 2026-08-04 后不可用。

---

## 0. 本次会话为何终结

- 用户发现 r5 团队有 worker 实际运行模型非 lite（当时 lite 映射 hy3；疑似 deepseek-v4-flash，违反模型纪律）。
- 用户将当前顶层模型切到 **hy3（lite）**，要求"彻底重建团队"。
- 但随后决定**本次会话直接终结**，重建动作交给下一会话。
- 已要求 r6 在途 3 个 worker 全部保护在途进度；现已全部保护完毕（见下）。

## GOAL（不变，贯穿所有会话）
mcc/m++ 重构，最终 C++ 覆盖 98~23、C 覆盖 90~23，完全自举、端到端 pass、结构清晰；标准实现同时做自主特性/性能/质量优化。

## 1. 团队现状（mcc-team-r6）
- r6 仅 3 个 worker 在途：**grace / bella / chloe**，全部已保护进度，无在途丢失风险。
- 重建计划：下一会话 `TeamDelete(mcc-team-r6)` → 建 `mcc-team-r7`，凡 spawn 显式指定 model（当时为 lite/hy3），**严禁复用 r6 worker、严禁 default 变体**。

## 2. 待归并分支（基线 worktree-mxx-work = 43d1507）
r5 关闭遗留 7 分支，全部在 origin：

| # | 分支 | HEAD | 内容 |
|---|---|---|---|
| 1 | worktree-tmp-eve-p4step1 | 65312f6 | Phase 4 step1 删 emit.c 直接-LIR 块（-602 行） |
| 2 | worktree-tmp-diana-errcode2 | 7d6027b | 错误码全覆盖 E0005-E0012（E0000 剩余 0） |
| 3 | worktree-tmp-hazel-bench | 788c2de | 性能基准集 + bench-report（7 条优化建议） |
| 4 | worktree-tmp-hazel-aafill | bbb83d0 | aarch64 MIR-native 全功能补齐 |
| 5 | worktree-tmp-bella-la64fill | e644c6b | loongarch64 MIR-native 全功能补齐 |
| 6 | worktree-tmp-chloe-arm | a85dc8b | arm MIR-native 移植（标量+浮点，qemu-arm 通过） |
| 7 | worktree-tmp-bella-perf | e6978e2 | MIR 机器层优化（regalloc 6.6×/load 转发/-33%/-O3 sdiv） |

**grace 已保护进度**：在 mxx-work 合入前 6 个分支（含全部冲突手工解决），push 到
`worktree-tmp-grace-merge-wip`（顶端 **17829c8**），随后 reset mxx-work 回干净 43d1507。
**第 7 个 bella-perf 未合**，留给重建后接手。

## 3. r6 worker 收尾状态
- **grace**（#1 归并）：completed（进度保护）。wip=17829c8（6/7 合入）。下一步：r7 新 worker `merge --no-ff origin/worktree-tmp-grace-merge-wip` → 合 bella-perf → 双模式 verify-all → push。
- **bella**（#2 验证）：completed。43d1507 上 verify-all **19/19 双后端**通过。ahead 5 = r5 bella-perf（已 push，即上表 #7，无需保护）。工作树干净。
- **chloe**（#3 优化）：无源码进度（分支=43d1507，可丢弃），但完成**高价值调研**（见 §4）。已停。

## 4. #3 参数 ABI hint 优化 — 接手要点（chloe 调研）
- 根因**不在** `x86_64_mabi.c:mabi_selpar`（那里只是干净寄存器→虚拟值 mov）。
- 真正根因：前端 irgen 给每个参数建 `alloca` 槽并全程 load/store 访问，MIR 管线**缺 mem2reg/alloca 提升 pass**。`run_mir_passes`（`projects/mcc/src/mir/passes.c:863`，列表 `:875-882`）只有 FOLD/COPY/GVN/DCE。
- **首选方案**：加 mem2reg pass，把"未取地址、仅 load/store、标量"的 alloca 槽改写为直接 SSA 值引用，一次性消灭 `mov r10,slot; mov (r10),eax` 间接。
- **门禁**：`projects/mcc/src/mir/ssa.c:29` 的 `mssa_check` 维持单定义不变式，改写违例会打印 `SSA consistency check FAILED`。
- 默认路径与 `MCC_MIR_BACKEND=1` 生成 asm 逐字节相同，改一处两模式同步，但**两模式门禁都跑**。

## 5. 关键纪律/环境提醒（接手必读）
- **构建依赖**：纯净 worktree 须先 `make -C ../meuos-sysroot` 与 `make -C ../meuos-toolchain` 生成 libmsys.a，否则 `make` 报 `cannot find -lmsys`（bella 验证发现）。`/tmp/mxx-verify-r6` 是已建好依赖的只读验证 worktree，可复用省构建。
- **共享 stash 栈**：`git stash list` 有 stash@{0/1/2}，**勿 pop/drop 他人 stash**（stash@{1}=alice rvalue ref+va_arg 在途，保留）。半成品一律 commit+push wip 分支。
- **门禁纪律**：凡 MIR 后端/机器代码改动，必须在 `MCC_MIR_BACKEND=1` 模式复验（仅默认 bridge 全绿不代表 MIR-native 正确）。
- 提交文件级 `git add`，禁 `git add -A`；遇夹带维持现状不 force push；核心交付前禁合并 main。
- 已知脆弱性：alice 的 `cli-args.sh` grep 只匹配 `cc ` 前缀，与 `HOST_CC=/usr/bin/gcc` 冲突（eve 发现，非其引入），待小修。

## 6. 下一会话动作清单（建议顺序）
1. `git fetch origin`；核实 mxx-work 干净（grace 已 reset 43d1507）。
2. `TeamDelete(mcc-team-r6)` → `TeamCreate(mcc-team-r7)`。
3. spawn grace（r7, model=lite）：`merge --no-ff origin/worktree-tmp-grace-merge-wip` → 合 bella-perf → 双模式 verify-all 19/19 + 自举 → push worktree-mxx-work。
4. spawn bella（r7, model=lite）：在合并后 HEAD 上重跑双模式 verify-all 19/19 独立验证（基线结论仅对 43d1507 成立）。
5. spawn chloe（r7, model=lite）：接手 #3，按 §4 调研实施 mem2reg pass；或派新 worker。
6. 视容量派新 worker 推进 GOAL（C++/C 覆盖、新优化建议等）。

---
*权威档案：`.agents/worker-deployment.md` §3 + §3.1 + §4 + §5。本文件过期以 worker-deployment.md 为准。*
