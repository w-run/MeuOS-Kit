# MIR 直接后端进度（progress.md）

> 运维约束：每完成一个可验证的里程碑立即 `git commit + push`，并更新本文档。
> 若因 API/网络中断，从最近的 git 提交 + 本文档续接。

## 概览

- **目标**：MCC 的 MIR 原生后端（MFnM 机器层，替换 LIR bridge 路径），
  新后端代码在 `src/mir/`、`src/target/x86_64/x86_64_m*.c`；参考源
  `src/target/x86_64/x86_64_isel.c` 等**不修改**。
- **开关**：`MCC_MIR_BACKEND=1` 启用新后端（标量函数）；聚合/varargs/TLS
  fallback 到 bridge。`MCC_DEBUG_MBE=1` 输出机器层 dump。
- **Oracle 基线**：`/tmp/mir-backend-base-p2/asm/`（123 测试 bridge 产物，
  P2 时更新；bridge 路径必须字节一致）。
- **P4 设计**：`docs/mir-backend/regalloc-design.md`（isel-debug，A→E 增量）。

## Phase 状态

| Phase | 内容 | 状态 | 提交 |
|-------|------|------|------|
| P0 | bridge 冻结为 oracle（collect.sh + 123 基线入库） | ✅ | 74e9321 |
| P1 | 机器层：MVal/MAddr/MMOP/MFnM，MReg target 化（6 架构） | ✅ | d42efce, 1a5eacd |
| P2 | x86-64 SysV ABI 移植（typclass/selpar/selcall/selret/va） | ✅ | a6c64e7 |
| P3a | MIR 类型系统：func_to_mir 建 MTypeDesc，MV_TYPE 双轨(id+td) | ✅ | 0b07fa8 |
| P3b | 完整 isel + emit：MFnM→x86-64 asm，可运行 | ✅ | eca97b7 |
| P4a | regalloc：活跃区间构造（mreg_intervals） | ✅ | 5e87cce |
| P4b | regalloc：槽分配（mreg_slots，slot4/8 打包） | ✅ | dc0de61 |
| P4c | regalloc：线性扫描（mreg_scan，调用点双池+fixed 占用） | ✅ | 75a5d15 |
| P4d | regalloc：phi 边移动（phi 值强制 spill 避免并行移动覆盖） | ✅ | 7c93bcd |
| P4e | regalloc 接管 emit（寄存器感知 + callee-saved 保存 + 静态 alloca） | ✅ | 待提交 |
| P4c | regalloc：线性扫描（mreg_scan，调用点双池） | ⏳ | — |
| P4d | regalloc：phi 边移动（机器层 phi 已降级为 pred copy） | ⏳ | — |
| P4e | regalloc 接管 emit（寄存器感知 + prologue/epilogue 保存 callee-saved） | ⏳ | — |

## 验证结果（截至 P3b）

- `make check-mir`：mir_test / pass_test / machine_test(86) / mabi_test(43) /
  bridge 全过。
- `test/c99 + test/c11` 在 `MCC_MIR_BACKEND=1` 下编译 + 运行 **0 失败**。
- 123 测试 bridge 路径 `.s` 与 P2 oracle **0 差异**（硬性回归标准）。
- 新后端运行正确：hello（printf）、fib(10)=55（递归+分支）、gcd/sumsq/浮点/
  extern/complex 均过。

## 当前工作点

- **P4a 进行中**：`src/mir/regalloc.c` 的 mreg_intervals——机器层无
  MVal.use 链（maddm 不维护），改用**指令数组扫描**收集 def/use 构造区间。

## 关键决策/发现

- 机器层 MInsM 直接持有 MVal*（src/dst/addr），无反向 use 链；
  regalloc 区间从指令扫描构造（非设计文档假设的 MUse 链）。
- 机器层无 MPhi（P3b 已把 SSA phi 降级为 pred 块尾 MMOP_MOV copy），
  P4d 主要为确认这些 copy 的寄存器分配正确。
- P3b emit 用"全栈虚拟值"方案（正确但未优化）；P4 由 regalloc 决定
  MV_TEMP 的 `reg`（物理寄存器）或 `slot`（栈槽），emit 改为寄存器感知。
