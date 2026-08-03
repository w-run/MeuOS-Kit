---
name: MIR 有符号 pow2 div/rem 缺陷 V 已闭环
description: mcc/m++ MIR msimp 有符号 div/rem 误编译缺陷（编号 V）已修复（93ab4b4）+ 回归收口（4c24bfe），verify-all 6/6
type: project
---

缺陷 V（MIR msimp_block 把有符号 div/rem 重写成 SAR/AND，负数结果错；-7/2=-4 应 -3、-7%4=1 应 -3）已闭环。

**Why:** worker-fold 2026-08-03 发现并通报；修复代码（passes.c:462-470 switch 只剩 MOP_UDIV/MOP_UREM 做 pow2 削减，有符号保留真实指令）随 93ab4b4（defect R 提交）夹带合入；回归测试在 4c24bfe 收口：pass_test Test 3b/3c（结构，非折叠路径不引入 SAR/AND）+ Test 3d test_fold_signed_pow2_values（常量折叠路径按值断言 -7/2、-7%4、-17/8、-17%8，守护 constexpr）+ test/c99/signed_div_pow2.c 端到端四用例。

**How to apply:** 编号为 V（U 已被 size-0 类值传参缺陷占用，worker-doc 澄清）。verify-all 6/6 PASS 含自举 check-sysroot-static；check-c99 与 check-c-mir（MIR=1==MIR=0 双路径矩阵）自动收集 signed_div_pow2.c。修复后 mcc 自举及 C++ 负数取模/除法场景不再受影响。若再遇负数 div/rem 异常，先查新引入而非本缺陷复现。worker-cpp20 确认 constfold 路径本就正确（-7/2 编译期按 C 语义折叠，不走 shift）。

**门禁缺口（worker-judge 2026-08-03 审核发现）**：verify-all.sh 第 90 行只跑 `make check-mir`（MIR 单元测试），**未调用 `check-c-mir`（mir_matrix.sh 双路径矩阵，Makefile 210 行已有该目标）**——即 legacy 路径（MCC_USE_MIR=0）未被 verify-all 门禁显式验证。signed_div_pow2.c 的 legacy 验证靠手动跑（worker-judge 实测双路径均 PASS）。已建议 worker-selfhost/worker-doc 把 `make check-c-mir` 纳入 verify-all.sh，第二轮处理。
