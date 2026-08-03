---
name: MIR 后端缺陷闭环
description: 有符号 div/rem (V)、空类 ABI (U家族)、mem2reg/LOADFWD 分工、slotmerge 自举失败 (J)
type: project
---

# MIR 后端缺陷闭环

## 1. 有符号 pow2 div/rem（缺陷 V，已闭环）

msimp_block 把有符号 div/rem 重写成 SAR/AND，负数结果错（-7/2=-4 应 -3）。修复随 93ab4b4 合入 + 4c24bfe 回归收口（pass_test Test 3b/3c/3d + test/c99/signed_div_pow2.c）。修复后 passes.c:462-470 switch 只剩 MOP_UDIV/MOP_UREM 做 pow2 削减，有符号保留真实指令。

**门禁缺口（worker-judge 发现）**：verify-all.sh 第 90 行只跑 `make check-mir`，**未调用 `check-c-mir`（mir_matrix.sh 双路径矩阵）**——legacy 路径（MCC_USE_MIR=0）未被门禁显式验证。建议把 `make check-c-mir` 纳入 verify-all.sh。

## 2. 空类 ABI（缺陷 U 家族，已闭环）

MCC_MIR_BACKEND=1 路径空类/混合参数三处修复：
- **e4a885c**：mabi_typclass 空聚合 MT_NONE→MT_I64 归一（镜像 LIR 2be27a7）
- **00ed62b**：mdce_block 保留未使用 MOP_PAR（占寄存器槽）+ mreg_scan 入参寄存器 [0,pos) 前缀保留（防 selpar pad alloca 覆盖）

涉及 src/target/x86_64/x86_64_mabi.c、src/mir/passes.c、src/mir/regalloc.c。默认 LIR 路径不受影响。MIR 后端仍为实验路径。

## 3. mem2reg 与 LOADFWD 分工（勿误删其一）

管线（optlevel>=2）：`MEM2REG → COPY → LOADFWD → GVN → COPY`。
- **mem2reg**：只提升地址完全不逃逸、标量、类型一致的槽，跨块全覆盖，alloca 消失。
- **LOADFWD**（628f17b）：处理 mem2reg 拒绝的槽（地址逃逸/聚合/未定值读），块内 store→load 转发。
- **实测**：mem2reg+LOADFWD = 755840 行 asm，仅 mem2reg = 761824 行，LOADFWD 边际净减 5984 行。顺序不能反。

**bridge.c 多 phi 链式追加（归并必保）**：原 `qb->phi = phi;` 每轮覆盖，多 phi 只留最后一个。改为 `Phi **phitail = &qb->phi; ... phitail = &phi->link;`。任何产生多 phi 的 pass 都依赖它，若见 revert 成 `qb->phi = phi;` 是回归。

**MIR pass 通用踩坑**：移除指令会使 MVal.def 失效——删 store 就地改 MOP_NOP（留给 DCE），压缩数组后统一刷 def；不要边遍历边删。

## 4. slotmerge 破坏自举（缺陷 J，已禁用）

slotmerge pass（f22f3d7）在 -O2 下误编译 mcc 自身 pp.c/token.c，自举产物段错误。严格 A/B（仅注释 `P(slotmerge)` 即正常）确认为确定性误编译，非竞态。已禁用（97c8541）。BZ2 帧 3976→2152B 未达 <1000B。**后续讨论 slotmerge 修复时，必须把"自举 mcc 编译任意 C 文件"作为门禁**。
