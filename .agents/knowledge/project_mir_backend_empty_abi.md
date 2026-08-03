---
name: MIR 后端空类 ABI 修复闭环
description: MCC_MIR_BACKEND=1 路径空类/混合参数 ABI 三处修复（e4a885c+00ed62b），verify-all 6/6
type: project
---

缺陷 U 家族（空类按值传参/返回）在 MIR 原生后端（MCC_MIR_BACKEND=1，P3b/P4 实验路径，非默认）的三处 ABI 修复已闭环：

- **e4a885c**：mabi_typclass 空聚合（C++ class Empty{} size1）MT_NONE→MT_I64 归一（镜像 LIR 2be27a7 的 Kx→Kl）
- **00ed62b**：混合参数错位双根因——mdce_block 保留未使用 MOP_PAR（参数有 ABI 意义，即使未用也占寄存器槽）+ mreg_scan 入参寄存器 [0,pos) 前缀保留（防 selpar pad alloca 临时覆盖未消费的入参寄存器）

**Why:** worker-lambda 2026-08-03 报告 MIR 后端空类残留错位（调用方 RDI/被调方 RSI）；worker-fold 实证定位三层根因（分类缺失→DCE 删 PAR→regalloc 覆盖入参寄存器）。默认 LIR 路径不受影响（bridge 从 mfn->param 发射全部参数）。

**How to apply:** 涉及文件 src/target/x86_64/x86_64_mabi.c、src/mir/passes.c、src/mir/regalloc.c。verify-all 6/6 PASS（含自举）。若再遇 MCC_MIR_BACKEND=1 下参数错位/寄存器被覆盖，先查这三点；MIR 后端仍为实验路径，默认迁移前建议专项回归。
