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
| P4e | regalloc 接管 emit（寄存器感知 + callee-saved 保存 + 静态 alloca） | ✅ | 2664766 |
| P4fix | Bug 1/2 边界修复（浮点 isel + 栈传参寻址） | ✅ | d6483d5, 3ce88c0 |
| P5a | postra 冗余 mov 消除 + slot4/8 双游标复采 | ✅ | 33385f9, ab12b95, 5581557 |
| P5b | hint 优先级（ABI 边界寄存器倾向） | ✅ | ab12b95 |
| P6a | 聚合函数新后端（去 fallback：BLIT/sret/参数 pad） | ✅ | 待提交 |
| P6b | varargs 新后端（selvaarg/selvastart 机器层） | 🔄 进行中 | — |
| P4c | regalloc：线性扫描（mreg_scan，调用点双池） | ⏳ | — |
| P4d | regalloc：phi 边移动（机器层 phi 已降级为 pred copy） | ⏳ | — |
| P4e | regalloc 接管 emit（寄存器感知 + prologue/epilogue 保存 callee-saved） | ⏳ | — |

## 验证结果（截至 P3b）

- `make check-mir`：mir_test / pass_test / machine_test(86) / mabi_test(43) /
  regalloc_test(44) / bridge 全过。
- `test/c99 + test/c11` 在 `MCC_MIR_BACKEND=1` 下编译 + 运行 **0 失败**。
- 123 测试 bridge 路径 `.s` 与 P2 oracle **0 差异**（硬性回归标准）。
- 新后端运行正确：hello（printf）、fib(10)=55（递归+分支）、gcd/sumsq/浮点/
  extern/complex 均过。

## 边界 bug 修复（isel-debug P3b 独立验证，2026-08-02）

1. **浮点参数读取错误**：emit_addr 的 disp 错放括号内（`(8%rsp)`→`8(%rsp)`）、
   emit_mov 浮点经 xmm0 中转覆盖 ABI 参数寄存器（改直接 movsd，mem→mem 经 r10）、
   func_to_mir 未设 CALL 返回值 MVal.type（regalloc 误分 GPR，isel 补设）。
2. **>6 参数栈传参非法寻址**：selcall 的栈参数空间（SALLOC +stk）未恢复 →
   补 caller 清理 SALLOC -stk；emit 的 SALLOC 直接 subq $n（n 可负）。
3. **浮点指令选择**：map_op 按 dtype 选 FADD/FSUB/FMUL/FDIV/FNEG（isel 层）。

修复验证：fadd/fdiv/fneg 浮点参数、printf 7 参数全过；c99/c11 仍全过；
check-mir 全绿；bridge 路径 0 回归。

## 当前工作点

- **P4 regalloc 全部完成（A-E）**；isel-debug 验证发现的 P3b 遗留 2 个边界
  bug（浮点参数/运算、>6 参数栈传参）已修复并验证（提交 d6483d5 + 3ce88c0）。
- **P5 第一步完成**：`make check-sysroot-static` 自举回归通过（退出 0，
  MIR 新后端改动不影响默认自举链路）。
- **P5 第二步完成**：hello 走 MCC_MIR_BACKEND=1 新后端独立编译运行正确。
- **P5 第三步（postra）完成**：emit_mov 直接 mov（reg↔reg、reg↔mem 不经
  %rax 中转、同寄存器 no-op），add 指令量下降。
- **P5 第四步（hint + slot4/8 复采）完成**：slot4/slot8 双游标恢复
  （i32/f32 用 4 字节槽，emit 对 4 字节槽用 movl/movss），栈帧缩小；
  regalloc 分配优先 MVal.hint 寄存器（机制就位，暂未设置 hint 源）。
- **P5 全部完成**。MIR 新后端独立可用（hello 走 MCC_MIR_BACKEND=1）。
- MCC_MIR_BACKEND=1：标量函数走完整新后端（isel + ABI + regalloc + emit），
  聚合/varargs/TLS/VLA 动态 alloca fallback 到 bridge。

## Bug 修复记录（isel-debug 验证发现）

- **Bug 1 浮点**：isel 层浮点 ADD/SUB/MUL/DIV/NEG 选浮点 MMOP（FADD..FNEG，
  之前用整数）；emit 的 FNEG 用 0.0-x；MOVSX/MOVZX 从 rax 低位扩展；
  i32 除法从 eax 扩展。验证 fadd=9.0/fdiv=3.5/fneg=-7.0。
- **Bug 2 栈传参**：emit_addr 的 AT&T 格式错误（disp 应在括号外、base 前
  无逗号）导致 `(,%rsp)` 非法寻址；selcall 生成配对的 ±SALLOC（caller
  cleanup），emit 按正负 subq/addq。验证 printf 7 参数正确。

## 关键决策/发现

- 机器层 MInsM 直接持有 MVal*（src/dst/addr），无反向 use 链；
  regalloc 区间从指令扫描构造（非设计文档假设的 MUse 链）。
- 机器层无 MPhi（P3b 已把 SSA phi 降级为 pred 块尾 MMOP_MOV copy），
  phi 值经 `extra=1` 标记强制 spill（P4d，避免并行移动覆盖）。
- emit 的 scratch 寄存器（rax/rcx/rdx/r9/r10/r11/xmm0）从 regalloc 池排除
  （MTargetM.scratch），累加器不与操作数别名。
- 线性扫描的回边缺口（循环头入口值被循环体临时值覆盖）经"区间跨回边
  延伸"修复；`fm->regsused` 记录全部已分配寄存器（非当前活跃集）。
- 所有 spill 槽 8 字节对齐（emit 用 movq 访问），4 字节值不越界。
- 静态 alloca 用帧内 leaq 预留（不碰 %rsp），动态 alloca（VLA）fallback。
