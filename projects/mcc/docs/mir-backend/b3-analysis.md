# B.3 后端优化调研分析（bzip2 decompress.c）

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 目标：定位 mcc 与 gcc -O1/-O2 代码质量差距的根因，为后续优化定优先级。

## 1. 性能基线（decompress.c BZ2_decompress）

| 指标 | gcc -O2 | mcc -O2 | mcc -O1 | 比例 |
|:-----|:-------:|:-------:|:-------:|:----:|
| .text（全文件）| 11,203 B | — | — | 4.4x |
| 栈帧 | 160 B | 3,976 B | 4,472 B | 24.9x |
| mov 指令 | 1,081 | 6,905 | 8,003 | 6.6x |
| 栈访问指令 | 145 | 4,293 | 4,894 | 23x |

> mcc 指标为 `MEUOS_SYSROOT=... ./mcc -O1/-O2 -I/tmp/bzip2-1.0.8 -S decompress.c` 后对
> `BZ2_decompress` 函数体统计。gcc 数据引用 .issues/IR-DESIGN.md。

## 2. 根因分析（按影响排序）

### 2.1 栈帧 24.9x：coalesce（slot 生命周期合并）完全失效
- `postra` 输出：`BZ2_decompress, 1108 slots`——rega 把 1108 个 temp spill 到栈。
- 加调试确认 `mem.c coalesce()` 的 `nsl=0`：**找不到任何可合并的 slot**。
- 根因：coalesce 的收集条件要求
  `t->alias.type == ALoc && t->alias.slot == &t->alias`，
  而 ALoc 仅由 **Oalloc 指令**（`Oalloc <= op <= Oalloc1`）定义（alias.c fillalias L163-173）。
  **MIR 路径（默认，bridge→LIR）不生成 Oalloc**：isel 后 IR 中 grep `alloc` 为 0，
  局部变量直接以 `SLOT(s)` 引用。因此 coalesce 空转，slot 无生命周期复用，
  每个被 spill 的 temp 独占 slot → 栈帧膨胀。
- 佐证：gcc 复用生命周期不重叠的栈槽（160B ≈ 40 slot），mcc 独占总计 1108 slot。

### 2.2 mov 6.6x：phi 块间拷贝被 spill 成栈间搬运
- IR 统计：BZ2_decompress 有 **444 个 phi**、69 个 Ocopy、278 load、481 store。
- 汇编出现 **852 条 `movd 栈→xmmN; movd xmmN→栈`** 完全冗余对（如
  `movd -3836(%rbp), %xmm15; movd %xmm15, -2908(%rbp)`）。
- 来源：rega phase-4 块间协调拷贝（pmadd/pmgen）处理 phi 时，若 phi 的
  to/arg 都被 spill，则生成「load arg_slot→reg; store reg→to_slot」，
  临时寄存器取 xmm15 → movd 栈间搬运。
- `pmrec` 已跳过 `src==dst` 的情况，但 to/arg 分配不同 slot 时拷贝无法消除。
- 这是 coalesce 失效（2.1）的直接后果：若 slot 合并，phi 拷贝的 src/dst
  会映射到同一 slot 而被跳过。

### 2.3 copy 传播尝试（已回退）
- 实现 QBE 风格 `copy()`（src 单 def、仅被本 copy 用、同块定义→重写 dst 的
  use 为 src），接入 isel 后、filllive 前。
- 量化（BZ2_decompress）：
  - **-O1**：mov 8,003 → 7,934（-0.9%），stackaccess 4,894 → 4,886（略降）。
  - **-O2**：mov 6,905 → 7,151（**+3.6%，负优化**），stackaccess 4,293 → 4,285。
- 结论：copy 传播不稳定（-O1 正、-O2 负，与 gcm/ifconvert 交互后 rega 分配
  恶化），**已回退**。69 个 Ocopy 非 mov 冗余主源（852 movd 来自 phi）。

### 2.4 peephole 边界
- no-op mov（`mov %reg, %reg` 同寄存器）仅 7 个，收益可忽略。
- 连续相同 mov 0 个；交换 mov 3 个；立即数 store 合并 1 个。
- reg→reg mov 1,224 条基本必要（rega 并行移动/phi 拷贝），非纯冗余。
- 结论：mov 消除类 peephole 收益小，不是主要方向。

## 3. 优化建议（优先级）

| 优先级 | 优化 | 预期收益 | 复杂度 |
|:---:|:-----|:-------|:-----:|
| P0 | **修复 coalesce 失效**：MIR 路径生成 Oalloc（bridge/func_to_mir），或重写 coalesce 支持 SLOT 引用模型 | 栈帧 24.9x→接近 gcc，movd 冗余大幅减少 | 高（涉及 src/lir 或 src/c/irgen）|
| P1 | **rega phi slot 合并**：phi 的 to/arg 若都 spill 且生命周期兼容，分配同一 slot | 减少 phi 块间拷贝（movd）| 中 |
| P2 | spill 成本模型微调（跨调用/热路径细化） | 边际 | 低 |
| P3 | peephole（mov 消除等）| 极低（已测边界）| 低 |

> 注意：P0 涉及 `src/lir/`（bridge）或 `src/c/irgen`（func_to_mir），超出本任务
> 「只改 src/opt/」约束。建议作为独立任务（B.5 后的 MIR 完备项）处理。
> P1 在 src/opt/rega.c 内，可独立实现。

## 4. 验证方法（可复现）

```sh
# 基线
MEUOS_SYSROOT=$(pwd)/../sysroot ./mcc -O2 -I/tmp/bzip2-1.0.8 -S \
  -o /tmp/decompress.s /tmp/bzip2-1.0.8/decompress.c
# 统计 BZ2_decompress：栈帧 = prologue subq $N；mov/栈访问数见上文脚本
# 门禁：verify-all 6/6 + make check-sysroot-static（自举，mcc 严格查隐式声明）
```
