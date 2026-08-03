---
name: 4 分支归并 + MIR-native PIC GOT 回归
description: 2026-08-03 grace 归并 alice/diana/eve/chloe 到主线；x86_64 MIR-native -fPIC GOT 缺失回归修复
type: project
---

2026-08-03 mcc-team-r5，grace 归并 4 worker 分支到 worktree-mxx-work（基 7878f57，最终 HEAD 9742e2f，双模式 verify-all 19/19）。

- **归并**：alice-cpp20（68d1222）/ diana-errcode（9c1c16d）/ eve-olevel（a1bbb85）/ chloe-mirp2（6cafb11 + 更新版 318e184 于 7240133），仅 worker-deployment.md 冲突（eve 基于旧主线）。
- **memit.c TLS 裁决**：以 chloe g_pic 版为正版（20e6988），弃 bella T.pic 版（16273af 引 ir.h 破坏 MIR 纯度）。核对无 T.pic/ir.h 残留。
- **check-olevel 3 项已知差距**（MIR-native P2 后端真实未实现，已排 MIR 迭代）：① if-conversion/cmov（x86_64_mbe.c isel 仍 P2 seed）；② -O2 省略叶函数帧指针（各级别都保留 rbp）；③ -O1 内存局部常量传播（`int k=7; x+(k+1)` 的 k 走栈槽，FOLD 不跨内存）。O2 imul/O3 shl 强度削减、O9 钳制、Ox 拒绝、运行时正确均通过。
- **check-pic-verify x86_64 GOT 回归修复（97d5467）**：chloe Phase 2 强制 g_use_mir_backend=1 默认后，x86_64 走 MIR-native 后端（x86_64_memit.c），其对 -fPIC 发射 RIP 直接寻址丢失外部符号 GOT。修复：新增 emit_global_addr()（g_pic 下 `movq sym@gotpcrel(%rip), %reg`）、emit_addr_loads/mov_to_rax 的 MC_ADDR 路径改用、MMOP_CALL 对 g_pic 外部符号补 @plt。四架构 pic-verify 全过。

**MIR-native 后端（x86_64_mbe.c/x86_64_memit.c）注意**：
- 输出用 TAB 分隔（`pushq\t%rbp`），`grep 'pushq %rbp'`（空格）匹配不到——测试 grep 需 `\s*` 容错。
- MV_GLOBAL 有 isext 字段（外部符号），可据此发射 @plt/@gotpcrel。
- g_use_mir_backend 默认=1（chloe Phase 2 强制），MCC_MIR_BACKEND=0 可回 LIR。
