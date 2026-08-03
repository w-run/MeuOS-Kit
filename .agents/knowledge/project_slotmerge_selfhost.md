---
name: slotmerge 自举失败发现
description: P2 slotmerge (f22f3d7) 破坏 mcc 自举：mcc 编译 mcc 后产物段错误；定位 pp.o/token.o 被误编译；BZ2 帧仅 2152B
type: project
---

2026-08-02：slotmerge pass（commit f22f3d7，task #6）验证结论：

- **正确性 bug（阻塞，已定名 defect J）**：slotmerge 在 -O2 下误编译 mcc 自身 pp.c 与 token.c 各至少一个函数。自举（mcc 编译 mcc）产物编译任意 C 文件段错误（rc=139，空文件 -E 也崩）。严格 A/B：同一提交树仅注释 `passes.c` 的 `P(slotmerge)` 后自举完全正常。对象级二分定位：`build/src/c/lex/pp.o` 与 `token.o` 各自单独换入即复现崩溃。
- **推翻**了 worker-cpp 文档（.issues/0802.md "sysroot-static FAIL 既有问题"）的判断——实际是 slotmerge 引入。
- **性能未达标**：BZ2_decompress 帧 3976→2152B（-45%，984→533 slots，-dP 可查），目标 <1000B 未达；瓶颈是保守模型（循环/单点/RMem slot 不合并）。
- **已修复门禁**：worker-cpp 提交 97c8541 禁用 slotmerge（defect J），verify-all 恢复 **6/6 全绿**。
- **通过项**：verify-all 其余 5 项；slot 别名压测（/tmp/slotmerge_stress.c，直线/循环/RMem 三路径）与 gcc oracle 完全一致。
- **2026-08-02 深夜复核（定性冲突最终解决）**：worker-cpp 曾声称"自举 15/15 全过、之前是并发竞态"，经铁证厘清：其 selfJ（slotmerge 激活、可用）做宿主再自举一代 → gen2 产物 4/4 SIGSEGV。真相：selfJ 自身是"干净的"，因其构建宿主是 slotmerge 禁用版（当前树默认），从未真正测过"slotmerge 编译 mcc 自身"。**defect J = slotmerge 编译自身确定性误编译，任何激活宿主自举必坏（j-recheck 与 selfJ 两独立宿主均复现），非竞态**。崩溃症状：pp/token 被误编译函数破坏 rbp（callee-saved），mcc_main 在 `mov -0x72c(%rbp)` fault（RIP=0x43f3e0），另有输入路径死循环。gate 应保持 97c8541 禁用。

**Why:** 合并正确性模型（线性指令序号区间 + 循环闭包）存在系统性问题，pp/token 两文件同时中招说明不是偶发。
**How to apply:** 后续讨论 slotmerge 修复/复测时，必须把"自举 mcc 编译任意 C 文件"作为门禁；对拍环境在 /tmp/sysh-noslot（好）与 /tmp/sysh-keep（坏），bisect 目录 /tmp/sysh-bisect，无 slot 宿主 mcc 在 /tmp/mcc-noslot-copy。

## 后续补充（同日深夜）：近期 check-sysroot-static FAIL 定性为并发竞态
- 上述 defect J（slotmerge 误编译）是 f22f3d7 时代的真实缺陷且已禁用（97c8541），**已闭环**。
- 但 worker-test 深夜报告的"无 slotmerge 也 FAIL"经 worker-cpp + worker-sysroot 串行验证（5 个独立目录 rc=0，含 mxx-work 现场、base-mcc/clean-18b/clean-625d 三个干净 checkout、verify-all.sh 6/6 PASS）确认根因是**多 worker 并发 make 竞态**（互相覆盖 build/、sysroot、libmsys.a），非代码缺陷。0802.md 已追加定性更正。
- **How to apply:** check-sysroot-static 会重建 sysroot 并写 /tmp/mcc-sysroot-static.XXXXXX，**禁止多 worker 并发在同一 worktree 上跑该 target**；回归验证统一在独立干净 checkout 串行执行。