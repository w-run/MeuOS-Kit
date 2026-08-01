# MIR 后端进度（MIR-native backend progress）

> 维护人：mir-backend。**每完成一个可验证里程碑立即 git 提交推送**，
> 中断恢复从本文件 + git log 续接。目标：完全抛弃 QBE LIR，仅用 MIR
> 做指令选择/寄存器分配/汇编生成（纯净准则：无 Fn/Ins/Ref、无 fill*、
> 无 Oflag/Osel）。

## 路线总览（team-lead 批准，路线 b：移植 proven 算法到 MIR 原生）

```
C/C++ 前端树 → func_to_mir (MFn) → MIR passes → [机器层 MFnM] → asm
   P3a 类型系统补齐      P3b isel        P4 regalloc  P5 emit 完善
```

MCC_MIR_BACKEND=1 开启新后端（per-function fallback 到 bridge 路径）。

## Phase 状态

| Phase | 内容 | 状态 | 提交 | 验证 |
|:------|:-----|:----:|:-----|:-----|
| P0 | 冻结 bridge 为 oracle，采集 123 测试基线 | ✅ | 74e9321（基线入库 docs/mir-backend/asm） | 123 测试，oracle 在 /tmp/mir-backend-base-p2 |
| P1 | MIR 机器层类型（寄存器/寻址/MMOP/MCC/MFnM） | ✅ | d42efce + 1a5eacd | machine_test 86/86 |
| P2 | x86-64 SysV ABI 移植（typclass/selpar/selcall/selret） | ✅ | a6c64e7 | mabi_test 43/43；123 基线 0 回归 |
| P3a | func_to_mir 类型系统补齐（MTypeDesc 树，MV_TYPE 双轨 id+td） | ✅ | 0b07fa8 | 聚合 ABI 跑通（8B reg-pack/sret/浮点聚合） |
| P3b | 完整 isel + emit（MFn→MFnM→x86-64 asm，可运行） | ✅ | eca97b7 | hello/fib 运行正确；c99/c11 全过；123 基线 0 回归 |
| P4 | 寄存器分配（线性扫描/图着色替换全栈槽映射） | ⏳ 待办 | — | — |
| P5 | emit 完善（PIC/TLS/varargs 全支持），开关翻转 | ⏳ 待办 | — | — |
| P6 | 删除 QBE LIR（include/ir.h Fn/Ref、src/opt/*、src/lir/） | ⏳ 待办 | — | — |

## 各 Phase 验证结果

- **P0**：123 测试（35 C + 84 C++ + 4 代表程序），116 OK / 6 expected-fail（负向诊断）/ 1 unexpected（cpp_staticmeth，m++ 缺口）。oracle 更新历史：P0 初始 → P2 更新（含 a270435 LIR 越界读修复的 14 个 cpp 差异，确认非 P2 引入）。
- **P1**：MReg target 化（MTargetM/寄存器表/gpr,fpr 范围/ptrsize），x86_64 表在 include/x86_64_m.h。check-mir-machine 86/86。
- **P2**：mabi_typclass/argsclass/selpar/selcall/selret 吃 MTypeDesc；MCC_MIR_BACKEND 平行验证开关；check-mir-abi 43/43。
- **P3a**：fe_to_mtd 建 MTypeDesc 树（MField 显式偏移/位域/数组/嵌套/union）；emittype.c typeforvalue 反向映射；MV_TYPE 保留 id（bridge）与 td（机器层）。聚合函数不再 skip。
- **P3b**：完整 isel（比较/转换/除法/phi/大立即数/全局地址）+ emit（全栈虚拟值方案）。**hello/fib(10)=55 新后端运行正确**；test/c99+c11 全套在 MCC_MIR_BACKEND=1 下 0 失败；123 bridge 路径与 oracle 字节一致。

## 当前工作点

- **正在做**：P3b 已完成（eca97b7）。等待 P4（寄存器分配）指示。
- **下一步（P4）**：线性扫描或图着色替换 x86_64_memit.c 的全栈槽映射。
  MIR 是显式 SSA，regalloc 直接设计（不移植 QBE de-SSA 后 rega）。
  保留现有 isel 指令选择层（mbe.c），仅替换 emit 的虚拟值→物理寄存器解析。
  参考 docs/mir-backend/regalloc-design.md。

## 常用命令

- 新后端：`MCC_MIR_BACKEND=1 ./mcc --specs=host -o out -I<meuos-libc>/include src.c`
- 新后端 dump：`MCC_DEBUG_MBE=1 MCC_MIR_BACKEND=1 ./mcc ...`
- MIR 测试：`make check-mir`（types/passes/machine 86/abi 43/bridge）
- 基线采集：`bash docs/mir-backend/collect.sh ./mcc ./m++ /tmp/mxx-t <outdir>`
- 基线对比：`diff -rq /tmp/mir-backend-base-p2/asm <outdir>/asm`

## 关键决策记录

- **bridge 与机器层共存**：MV_TYPE MVal 同时保留 id（前端/LIR typ[] 索引，
  bridge 发 TYPE(idx)）与 td（MTypeDesc，机器层用）。聚合参数 mv->td、
  聚合返回 fn->rettyd。
- **MOP_STORE 约定**：实际是 src[0]=值, src[1]=地址（mir.h 注释过期），
  funcstore 生成 funcinst(qt.store, 0, v, lval.addr)。mbe.c 已按此处理。
- **P3b emit 方案**：全栈虚拟值（每个 MV_TEMP 一个 8B 栈槽），算术经
  %rax、浮点经 %xmm0、寻址 base/index 载入 r10/r11、浮点常量 per-function
  .rodata 池。正确但未优化，P4 替换。
- **混合模式**：mbe_supported() 对聚合/varargs/TLS/SALLOC 函数返回 false
  → 走 bridge。MCC_MIR_BACKEND=1 是 per-function 选择。
