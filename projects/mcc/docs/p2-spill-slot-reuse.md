# P2 性能专项：spill slot 生命周期复用（预研方案）

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 状态：**调研完成，方案定稿，未实施**。
> 目标：消除 b3-analysis.md §2.1 的根因——spill 产生的栈 slot 无生命周期复用，
> 栈帧 3976B → <1000B（gcc 160B 的对照基准）。

## 1. 背景与根因（读码结论）

b3-analysis.md §2.1 已定位：`BZ2_decompress` 的 rega 把 **1108 个 temp 全部 spill 到栈**，
每个 temp 独占一个永久 slot → 栈帧 3976B。

读码确认的机制链：

- **spill.c `slot()`（L149-181）**：为被 spill 的 temp 分配 slot 时用 **slot4/slot8 单调递增游标**，
  写入 `tmp[t].slot` 后**永久归属该 temp**，从不因生命周期结束而归还/复用。
  `fn->slot = locs + slot8`（L710）即为总栈帧大小。
- **rega.c `rref()`（L101-129）**：temp 若不在寄存器映射中，一律解析为 `SLOT(tmp[t].slot)`；
  另有 `fn_local_slot()`（L86-99）在 rega 期动态补 slot（spill 与 rega 的边界不一致兜底）。
- **postra.c**：只做"冗余 slot→slot Ocopy 消除"（删除 dead copy），**不改 slot 编号**，无复用。
- **mem.c `coalesce()`**（QBE 原版 slot 合并）：依赖 `Oalloc` 建立的 `alias.type == ALoc`
  （alias.c），而 **MIR 路径不生成 Oalloc**（b3 §2.1 实测 nsl=0），因此空转。
  该 pass 位于 isel **之前**（passes.c L35），与 spill 后的 SLOT 引用模型完全脱节。

**结论**：slot 复用缺一个"针对 spill 后 SLOT 引用模型"的合并 pass。
当前没有任何机制为生命周期不重叠的 spill temp 共享栈槽。

## 2. 核心难点（P1 回退的教训）

P1 slotmerge 曾实现但 **segfault 回退**。读码确认根因：

- `filllive()`（live.c）只追踪 **RTmp** 的 live（`b->in/out` 是 temp 位图）。
- `spill()` 遍历后 **重写**了 `b->out`（L527）——语义从"live temp 集合"变成
  "**必须留在寄存器中的 temp 集合**"，且指令中 spill temp 已被改写为 `RSlot` 引用。
- 因此 spill **之后**：
  1. 原 temp 的 live 区间信息（filllive 的 in/out）已被破坏；
  2. 再调 `filllive()` 无法追踪 `RSlot`（liveon/bset 只认 `RTmp`），得到的结果不可用。

P1 若在 spill 后直接复用/重建 live 结构，就会读到被改写或未初始化的数据 → segfault。

**方案红线**：任何 slot 复用逻辑**不得依赖 spill 后的 `b->in/out` live 结构**，
必须从指令流中的 `RSlot` 引用**重新推导** slot 生命周期。

## 3. 方案（推荐路径 b：独立后处理 pass `slotmerge`）

### 3.1 总体设计

新增 `src/opt/slotmerge.c`，pass 管线挂载在 **rega 之后、postra 之后**：

```
... → P(spill) → P(rega) → if(ol>=1) P(postra) → P(slotmerge) → P(fillcfg) → P(simpljmp) → ...
```

选 rega **后**：spill 的 SLOT 引用已是最终形式（rega 的 `rref`/`fn_local_slot` 也补齐了
漏网 slot），一次扫描即可覆盖所有 `RSlot`。选 postra **后**：postra 按 slot 编号追踪
Ocopy，slot 重编号会破坏其假设；顺序定为 postra → slotmerge 最稳。

### 3.2 Step 1：收集 slot 访问点

遍历所有块的**全部引用位点**（指令 `to/arg[0]/arg[1]`、`jmp.arg/arg1`、phi 的 `to/arg[]`、
RMem 的 `base/index`），对每个 `RSlot(s)` 记录：

```
(slot, block_id, 块内指令下标, 访问方向)
```

（RMem 的 base/index 若为 RSlot 也纳入；本后端 mem base/index 现为 RTmp，防御性覆盖。）

### 3.3 Step 2：计算 slot 的 live 区域（CFG 感知，从访问点推导）

**块内**：slot s 在块 b 的访问区间 `[first_b(s), last_b(s)]`（块内指令下标 min/max）。

**块间（关键，防循环跨迭代冲突）**：
- 对每个 slot，做一次 **反向数据流**（类 filllive，但对象是 slot 而非 temp）：
  - `out[s][b]` 由后继的 `in[s][succ]` 并集得到；
  - 块内指令逆序遍历：读 `RSlot(s)` → gen，写 → kill；据此算 `in[s][b]`。
  - **循环保守**：slot 在循环 L 内任一块 live → 扩展到 L 的所有块
    （用 `loopiter`/`b->loop`，防止"迭代 i 写、迭代 i+1 读"被线性近似误判为不重叠）。
- 每块的 `in/out` 仅需"该块内 slot 是否 live"，不记 temp。

简化实现的务实选项（首版）：不做全数据流，改用 **访问块 + 循环闭包** 近似——
slot 的 live 区域 = 其访问块集合 ∪ 这些块所在循环的全部块；块内用指令区间。
该近似**只高估、不低估** live，保证合并安全；首版收益略低但实现简单、易验证。

### 3.4 Step 3：贪心合并 + 改写 + 重编号

- 两 slot `a`、`b` 可合并 ⟺ live 区域**完全不相交**：
  - 无共同 block（含循环闭包扩展后的块集合不相交），
    即任一 block 上 `a`、`b` 不同时 live；
  - 同一 block 内：指令区间 `[first,last]` 不相交才合并。
- 按 live 区域排序后贪心（或 O(n²) 两两检查，n≈1108 → 约 120 万次区间比较，可接受）。
- 合并 `b → a`：把**所有** `RSlot(b)` 引用改写为 `RSlot(a)`，`a` 的 live 区域取并集。
- 迭代直至无合并可能。
- **重编号压缩**：把存活 slot 号压紧（消除合并产生的空洞），`fn->slot = 最大存活号 + 1`，
  并保持现有对齐语义（spill 的 `slot8 += slot8 & 3` 16B 对齐由 emit 侧保证，见 §5）。

### 3.5 与路径 a 的对比（spill 时即合并）

| 维度 | 路径 a（改 spill.c `slot()`） | 路径 b（后处理 pass） |
|:-----|:---------------------------|:---------------------|
| 改动面 | spill 核心分配逻辑（slot4/slot8 游标 → 复用池），所有平台所有路径受影响 | 独立新 pass，不动 spill/rega 核心 |
| live 数据 | spill 时 filllive 刚算完，最准 | 从 RSlot 引用重新推导，稍保守 |
| 跨块复杂度 | spill 逆序遍历时前驱块 live 未知，slot 释放时机难定 | 数据流/循环闭包集中处理 |
| 回退性 | 差（波及面大） | 好（`-dS` 对比 + 开关即可回退） |
| 结论 | P2 二期可选 | **首期采用** |

## 4. 预期收益

- **栈帧**：BZ2_decompress 1108 slots → 复用后预计 <100（gcc ≈40）。按每 slot 4B，
  3976B → 预计 **300~700B，目标 <1000B**（mcc -O2，gcc 160B 仍约 2~4x，属可接受的
  剩余差距，后续可再叠路径 a 的块内精确区间）。
- **movd 冗余**：b3 §2.2 的 phi 块间拷贝 `load arg_slot→reg; store reg→to_slot`，
  若 src/dst 合并到同一 slot，pmrec 的 `src==dst` 跳过生效，852 条栈间 movd 大幅下降。
- **栈访问指令**：spill/reload 的 load/store 总量随 slot 数下降而减少（间接收益）。

## 5. 关键正确性约束与风险

1. **循环跨迭代**（最高风险）：线性指令序号在循环内只出现一次，跨迭代读写会被误判为
   不重叠 → 必须用循环闭包扩展 live 区域（§3.3）。任何不确定即**不合并**（保守）。
2. **Ocall 边界**：call 指令可能读写栈（callee 的栈帧），call 点所有 slot 视为 live；
   合并条件中跨 call 的 slot 一律不相交处理。
3. **phi 与 jmp**：rega 后 phi `to/arg`、`jmp.arg` 可为 RSlot，访问点收集必须覆盖；
   遗漏会（a）错过合并（低危）或（b）改漏引用导致值错乱（高危，需全位点覆盖）。
4. **emit 布局**：emit 按 `-4*(fn->slot - s)` 定位 slot（x86_64_emit.c L203），`assert(s<=fn->slot)`
   ——slotmerge 后所有存活 slot 号 < 新 `fn->slot`，压缩编号后须满足该不变式。
   `e->fsz = 4*f`（L873）直接随 `fn->slot` 缩小，prologue 自动变小。
5. **与 postra 顺序**：postra 在 slotmerge 之前跑（§3.1），避免 slot 重编号破坏其
   last_def/last_def_src 追踪。
6. **多平台**：slot 语义对所有 target 一致（SLOT 是后端无关的 Ref），但 i386 的
   `kl_in_reg==0` 路径把 Kl temp 也放 slot，合并时 Kl 的 8B slot（slot8 分配 2 单位）
   与 4B slot（slot4）**不可互并**——合并需按 slot 宽度分组。

## 6. 实施步骤（供实施阶段执行）

1. 新建 `src/opt/slotmerge.c`（可在 mem.c 基础上改造，或独立文件），
   IR 接口：`void slotmerge(Fn *fn)`；`#include "ir.h"` 即可，无跨层依赖。
2. 实现 §3.2 访问点收集（覆盖 ins/jmp/phi/mem 全位点）。
3. 实现 §3.3 live 区域（先做循环闭包近似版，`debug['P']` 下 dump slot 区间供核对）。
4. 实现 §3.4 贪心合并 + 改写 + 压缩重编号（按 slot 宽度分组）。
5. passes.c 挂载：`P(spill); P(rega); if(ol>=1) P(postra); P(slotmerge);`。
6. 门禁：
   - `MEUOS_SYSROOT=... make check-all`（verify-all 6/6）；
   - `make check-sysroot-static`（mcc 自举，严格查隐式声明/布局回归）；
   - bzip2 复测：BZ2_decompress 栈帧统计（目标 <1000B）+ 运行正确性；
   - 各 target 抽样（x86_64 为主，arm/aarch64/i386 至少冒烟）。
7. 回退预案：若任一平台回归，`slotmerge` 暂以环境变量/`-O` 门控，保留代码回退路径。

## 7. 参考

- b3-analysis.md §2.1（栈帧根因）、§2.2（movd 冗余）、§3（优先级 P0/P1/P2）。
- src/opt/spill.c `slot()`（L149-181）、src/opt/rega.c `rref`（L101-129）。
- src/ir/passes.c（管线顺序）、src/opt/postra.c、src/opt/mem.c `coalesce`。
- include/ir.h：`RSlot`/`SLOT(x)`、`struct Fn.slot`、`Tmp.slot`。
