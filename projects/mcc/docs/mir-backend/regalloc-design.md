# MIR 寄存器分配设计预研（P4）

> 任务：为 MIR 机器层（MFnM/MInsM/MVal，P1/P2 产物）设计原生寄存器分配器，
> 替代 QBE 的 rega/spill（src/opt/rega.c + spill.c）。本文档是**只读预研**，
> 不修改源码。作者：isel-debug（2026-08-02）。
>
> 结论先行：MIR 层有两个关键优势——(a) 显式 SSA（MVal.use 链现成）；
> (b) 机器指令已带物理寄存器占位（MV_REG）且 ABI lowering 已做（P2）。
> 因此推荐 **线性扫描**（linear scan）为主、**循环感知的 spill 预算**为
> 修正项，而不是照搬 QBE 的"逐块抢占式图着色"。与 QBE rega 的逐块
> RMap + 边拷贝机制相比，线性扫描天然适配 SSA 的活跃区间，且能复用
> QBE 已验证的 slot 打包不变量。

## 0. 术语与输入

- MFnM：机器函数（`include/mir.h`）。`slot/salign/regsused/nspill` 字段已
  预留（regalloc 输出）。`MBlkM.ins[]` 是 MInsM 指令数组，`term` 是终结符。
- MInsM：机器指令。`dst/src[3]/cst/addr/cc/td`。
- MVal：SSA 值。`kind`（MV_TEMP/MV_REG/MV_CONST/...）、`def/defphi/defblk`、
  `use[]`（MUse 链：`ins/phi/argn`）、`slot`（spill 槽，-1 未分配）、`hint`、
  `reg`（MV_REG 的物理寄存器 id）。
- MTargetM：目标描述。`regs[]`（MRegInfo：cls/caller_saved/callee_saved/arg）、
  `gpr0/ngpr/fpr0/nfpr`、`rglob`（SP/FP）、`reserved`（隐式暂存）、
  `argreg[]/rsave[]/rclob[]`、`ptrsize/stackalign/kl_in_reg`。

当前状态：P2 已完成 MFn→MFnM 转换 + x86_64 SysV ABI lowering
（src/target/x86_64/x86_64_mabi.c），P3 isel 在途。regalloc 运行在
**ABI lowering 之后**（此时参数/返回值已在物理寄存器中，调用点已生成
ARG/PARM/CALL/RET 边界拷贝）。这与 LIR 管线 `T.abi1 → T.isel → spill →
rega` 的顺序一致：MIR 侧为 `isel → [本方案] regalloc → emit`。

---

## 1. QBE rega/spill 正确性规则复盘（必须继承的不变量）

现有实现约 1500 行（rega.c 869 + spill.c 702），是 QBE 移植并经
multi-arch 修复。其正确性依赖以下规则，MIR 移植**不得打破**：

### 1.1 liveness（filllive，live.c）
- 数据流：`out[b] = ∪_{s∈succ} liveon(s)`；`liveon` 处理 phi：清除 `s` 的
  phi 目标、加入 `b` 作为前驱对应的 phi 实参（`b->gen` 也记录）。
- 反向迭代 rpo 至不动点。每条指令反向处理：`to` 杀死（从 in 删除、gen 加），
  `arg` 生成。`jmp.arg` 的 RCall 特判（retregs/argregs 位）。
- `nlive[2]`（GPR/FPR 峰值）在迭代中累计，供 spill 预算用。
- 关键不变量：`b->in` 是"块入口必须已就绪的值"集合；`b->out` 是"块出口
  必须存活的值"集合。rega 的 beg/end RMap 与之对齐。

### 1.2 fillcost（spill.c）— spill 成本
- `loopiter` 计算块嵌套（`fillloop`：块在 k 层循环内则 `b->loop *= 10`）。
- 每个 temp 的 `cost += loop`（每处使用/定义所在块的 loop 值累加；
  phi 实参按前驱块的 loop）。`ndef/nuse` 统计。
- `t->cost = t < Tmp0 ? UINT_MAX : 0`（物理寄存器永不 spill）。
- 目的：优先把高循环频率的值留在寄存器。

### 1.3 spill（spill.c）— 逐块限制活跃集
- 块按 rpo 逆序处理（保证后继块的 in/out 已定）。
- **back-edge**（`s1/s2->id <= b->id`，即回边到循环头 hd）：把 `b->out`
  限制为 hd 上 liveness 的活值，且预算按 `hd->nlive[k]`（循环内峰值）扣除
  "live through" 的值——这是循环感知的核心。
- 非回边：`merge()` 合并两个后继的 liveon，循环内层块的 temp 若已有 slot
  则优先 spill（`slot == -1` 才加入 u，即**已 spill 的不重复留寄存器**）。
- `limit/limit2`：按 class 分开限制到 `nallocgpr/nallocfpr - k`；用 cost
  排序保留最热的；`slot(t)` 给被踢出的分配栈槽。
- **slot 分配（关键不变量）**：`slot4/slot8` 双游标打包（4 字节槽与 8 字节
  槽交错），`slot8 >= slot4` 恒成立；`locs = fn->slot`（前置的 alloc 槽）；
  结束时 `fn->slot = locs + slot8`，并对齐 16 字节。
- **调用点**：`dopm` 处理连续 regcpy；遇到 `Ocall` 时把 v 限制到
  `T.nrsave[0/1]`（caller-saved 预算），先 spill 回边拒绝的值（`slot(t)`
  兜底分配），`v->t[0] &= ~retregs/rsave`，`|= argregs`。
- `b->out`/`b->in` 最终被替换为"必须在寄存器中的 temp 集合"（已 spill 的
  从集合剔除，改走 slot）。`b->jmp.arg` 若被 spill 则改写为 `SLOT`。

### 1.4 rega（rega.c）— 逐块寄存器映射
- **块处理顺序**：`carve` 排序（loop 从内到外，同层按 id 降序），保证
  内层循环先分配。`b->loop` 传给 `sethint`（权重）。
- **进入块**：用 `b->out` 建 `cur` 映射（按 `prio2` 排序 temp：有 visit
  的优先 → 有 hint 的优先 → cost 高者优先），先 `ralloctry(1)` 试探再
  `ralloc` 落实；物理寄存器（`< Tmp0`）先 `radd(cur, r, r)` 自映射。
- **逐指令反向**：`emiti` 反向生成，`doblk` 内对每个 op：
  - Ocall：`rsave` 全杀，arg 寄存器自映射占位（防 ABI 栈参数偷寄存器）。
  - Ocopy 连串 → `dopm`（并行移动）。
  - 结果 temp：`rfree` 旧映射；物理寄存器写直接 `radd(cur,r,r)`。
  - 参数：`insert` 按 hint 优先级排序，逐个 `ralloc`；RMem 的 base/index
    也参与。
  - hint 重定位（`cur->w[rf]`）：若刚释放的寄存器 rf 是某 temp 的 hint，
    且空闲，则 emit 拷贝把该 temp 移过去。
- **块结束**：`end[n]` 快照；`beg[n]` 记录（清 phi 目标后）。
- **多前驱边拷贝（phase 3）**：对 `npred > 1` 的块，计算每个 beg 寄存器的
  前驱来源寄存器（phi 实参 + 普通活值），统一的来源就生成一条并行移动
  （插在块头，`pmgen` 处理环/链）。
- **剩余边拷贝（phase 4）**：单前驱或不统一的边，新建块（`newblk`），
  插 phi 拷贝 + `b->in` 活值的 reload/store，`b->loop = (b->loop+s->loop)/2`。
- **并行移动 pmgen**：`pmrec` 处理移动链与环；环用 Oswap；普通链用 Ocopy。
  这是**唯一**产生 Oswap 的地方。
- **rref/ralloc 兜底**：`ralloctry` 对 `!kl_in_reg` 的 Kl 与 x87 直接返回
  slot；`rref` 找不到寄存器时用 `tmp[t].slot`，slot 也 -1 则 `fn_local_slot`
  动态扩展帧（spill 与 rega 的 back-edge 判定不一致时的补丁，见 1.5）。

### 1.5 已知 gap（spill-vs-rega 一致性，已有兜底）
- 回边 `limit2` 可能把某 temp 保留在寄存器（`slot == -1`），但 rega 的
  phase 4 边拷贝需要它的 slot（`rref`）——注释标明这是"两个 spill pass
  判定不一致"，当前用 `fn_local_slot`（动态扩帧一字节）兜底。
- **MIR 方案应直接从源头消除**：活跃区间（live interval）一旦确定，
  寄存器常驻与 slot 常驻由同一份区间数据决定，不存在两 pass 判定分叉。

### 1.6 其他依赖
- rega 依赖 `rpo`（fillcfg）、`phi`（ssa）、`cost`（fillcost）、`out/in`
  （filllive）、`b->loop`（fillloop）。顺序：`fillcfg→ssa→...→abi1→isel→
  filllive→fillloop→fillcost→spill→rega→postra`。
- `postra`（src/opt/postra.c）在 rega 后清理冗余 move（rega 可能产生
  过多 Ocopy）。

---

## 2. MIR 原生分配器设计

### 2.1 总体架构

```
MFnM (ABI-lowered, post-isel)
   │
   ├─ mreg_liveness:  逐块反向传播，得 MVal 活跃集（复用 MUse 链）
   ├─ mreg_intervals: 每个 SSA 值 → [def_pos, last_use_pos) 活跃区间
   ├─ mreg_slots:     为必然 spill 的值分配栈槽（slot4/slot8 打包）
   ├─ mreg_scan:      线性扫描分配物理寄存器（含 caller/callee-saved 池）
   ├─ mreg_phiresolve: 边拷贝（phi 降级为移动，并行移动 + 新块）
   └─ 输出：MInsM.src/dst 改写为 MV_REG 或 MAddr(SP,slot)；fm->slot/
        regsused 填充
```

与 LIR 管线的对应关系：

| LIR 侧             | MIR 侧（本方案）            | 差异点 |
|:-------------------|:----------------------------|:-------|
| filllive           | mreg_liveness               | 输入 MUse 链，无需 gen/kill 位集 |
| fillcost + spill   | mreg_slots（区间驱动的预算）| 见 2.4 |
| rega               | mreg_scan                    | 线性扫描，非逐块抢占 |
| rega phase3/4 边拷贝 | mreg_phiresolve             | 同 pmgen 算法，SSA phi 直接驱动 |

### 2.2 活跃区间计算（mreg_intervals）

SSA 的天然优势：每个 `MV_TEMP` 有**唯一 def**（MIns.def 或 MPhi.defphi），
use 链是现成的 `MVal.use[]`。因此不需要数据流迭代求活跃集——区间可直接
构造：

- 给每个指令（含 phi 与终结符）分配**全局序号** `pos`（块内递增，
  块间按 rpo 递增，phis 位于块头）。
- 对每个 MV_TEMP v：
  - `start[v] = def_pos(v)`（def 指令的 pos；phi 取块头 pos）。
  - `end[v] = max(pos of all uses)`（use 链遍历；phi 实参取前驱块尾）。
  - 无 use 的 v：`end[v] = start[v]`（死值，仅 def 瞬间存活）。
- 区间 `[start, end)` 即活跃区间。**循环感知**：区间跨越回边时，把
  `end` 延伸到循环出口（或按 loopiter 的 header 展开），避免区间在回边
  处被截断导致重分配。这是对 QBE back-edge 特殊处理的显式化。

复杂度 O(V + U)，其中 V=值数、U=总使用数；无需 filllive 的不动点迭代
（SSA 下区间就是精确的 liveness）。

### 2.3 物理寄存器池（mreg_scan）

按 MTargetM 划分：

- **GPR 池**：`[gpr0, gpr0+ngpr)` 减去 `rglob`（RBP/RSP）与 `reserved`。
- **FPR 池**：`[fpr0, fpr0+nfpr)` 同减。
- 每个池按 `callee_saved` 再分两组；**调用点**（MMOP_CALL）处：
  - 存活跨调用的值**只能**用 callee-saved（rclob）池；
  - caller-saved（rsave）池在 CALL 处被清空（杀值），供被调方使用；
  - 这等价于 QBE 的 `v->t[0] &= ~rsave` + `limit2(v, nrsave[0], nrsave[1])`。
- `MV_REG`（ABI 参数/返回值占位）预占对应物理寄存器，作为区间的一个
  特殊"已分配"值（对应 QBE 的 `radd(cur, r, r)`）。

分配时按**类**（KBASE(type)：0=GPR/1=FPR）选池，跨类使用由 isel/ABI
边界拷贝保证（QBE 同款约定：`KBASE` 判定，不混池）。

### 2.4 spill 决策与栈槽分配（mreg_slots）

取代 spill.c 的逐块 limit/limit2，用**区间重叠计数**做全局决策：

- 对每个区间，计算其**并行度**（max 同时活跃的同类值数）。若超过池容量
  → 必须 spill。选择被 spill 的值用 QBE 同款启发：`cost`（MIR 侧等价
  fillcost：值在循环内的使用次数×loop 权重）+ 区间长度。
- 被 spill 的区间：`slot = mreg_slot_alloc(type)`，打包规则**直接继承**
  spill.c 的 slot4/slot8 双游标不变量（含 KWIDE 判定、16 字节对齐、
  `fm->slot` 累计、`salign` 维护）。
- **确定性**：同一函数多次编译产生相同槽布局（排序键：cost 降序、id
  升序），保证 asm-diff 可复现。
- **kl_in_reg==0**（i386/arm）：Kl 值强制 spill（继承 QBE 约定），
  `slot(t)` 必须非 -1；区间直接标记"内存常驻"。

与 QBE 的关键差异：QBE 在块边界做"必须寄存器"集合与逐块预算，spill 是
局部贪心；本方案在**全函数区间**上做一次全局决策，天然避免 1.5 的
spill-vs-rega gap（寄存器/内存归属由同一份区间数据决定）。

### 2.5 分配主体（mreg_scan，线性扫描）

按 pos 递增扫描：

```
active = []            // 当前活跃的区间，按 end 排序（min-heap）
for each interval [s, e) of class c, in pos order:
    free registers of pool c (caller-saved cleared at CALLs)
    expire: pop active where end <= s, free their reg
    if a register is free: assign, push active
    else: spill 决策
        - if 当前值 cost < 已分配中 cost 最低者：把已分配者 spill
          （其区间在 s 处截断，后续 use 重载），当前值进寄存器
        - else：当前值 spill（slot 直用）
```

- **hint 优先**：`MVal.hint`（来自 phi/ABI 边界拷贝，等价 QBE `sethint`）
  决定候选寄存器顺序；MOV 结果的 hint 让区间优先落在目标寄存器，减少
  边拷贝。
- **调用点处理**：扫描到 MMOP_CALL 时，先 expire 所有 caller-saved 池中
  的区间（它们的值已 spill 或不用），再把 callee-saved 池的可用寄存器
  清点；跨调用区间只可能已在 callee-saved 池。
- 结束：`fm->regsused` = 分配用过的物理寄存器位或（含 MV_REG 占位），
  供 emit 生成 prologue/epilogue（保存/恢复 callee-saved）。

### 2.6 phi 降级与边拷贝（mreg_phiresolve）

SSA phi 在机器层不存在，需降级为前驱边上的移动：

- 对每个 `MPhi(dst, args[blk])`：前驱 b 的边上插入
  `dst ← src_b`（src_b 是 b 中对应实参的分配位置，寄存器或 slot）。
- 用 QBE 的 **pmgen 并行移动算法**（pmrec：链/环检测，环用 `MOV` +
  暂存或 `XCHG`）处理同一边多组移动的冲突；MIR 无 Oswap 指令时可
  用 `MOV reg, reg` + 第三临时或按 QBE 的 `Oswap` 语义用 3 条 MOV 展开。
- 多前驱块：优先把移动插在**共同前驱**的块尾（QBE phase 3 的共享拷贝
  优化），否则新建边块（QBE phase 4）。
- 边块创建：`mblkm_new` + `mfnm_addblk`，`term = MMOP_JMP`，loop 取
  两端均值（对齐 QBE 的 `(b->loop+s->loop)/2`）。

### 2.7 postra（冗余移动消除）
- 可复用 LIR `postra` 的启发（识别 `MOV r, r` 与可合并链）或先跳过
  （asm-diff 只要求语义等价，不要求指令数相同）。

---

## 3. 与 QBE rega 的差异总结

| 维度 | QBE rega/spill | MIR 原生（本方案） |
|:-----|:---------------|:-------------------|
| 分配策略 | 逐块抢占式（RMap 前后向传播） | 全函数线性扫描（区间） |
| liveness | filllive 数据流迭代（位集） | MUse 链直接构造区间（SSA 精确） |
| 循环感知 | 回边 limit2 + hd->nlive 预算 | 区间跨回边延伸 + cost 权重 |
| spill 决策 | 逐块 limit，slot4/8 打包 | 全局并行度 + 同款 slot 打包 |
| 边拷贝 | phase3/4 多前驱/新建块 | phi 驱动的边移动 + pmgen 并行移动 |
| slot 归属 | spill 与 rega 可能分叉（fn_local_slot 兜底） | 单一区间数据，无分叉 |
| 物理寄存器占位 | `radd(cur, r, r)` 显式 | MV_REG 预占（P2 已就位） |
| 目标参数化 | `T.gpr0/ngpr/rsave/rclob/...` | `MTargetM.regs[]/gpr0/rsave/rclob/...`（同构） |

关键取舍：**线性扫描 vs 图着色**。MIR 是显式 SSA，活跃区间天然不相交
（同一值区间不重叠），线性扫描的 active-set 退化为按 end 排序的堆，最优
子结构清晰；图着色（QBE 是"逐块模拟着色"）在 SSA 下没有额外收益，且实现
复杂度高。若实测线性扫描在寄存器压力大的热点函数上劣化（spill 过多），
备选方案是**区间着色**（interval graph coloring，Chaitin-Briggs 的
SSA 特化），但建议先线性扫描 + 2.4 的 cost 加权决策。

---

## 4. 验证策略（MCC_MIR_BACKEND=1 下与 bridge oracle asm-diff）

### 4.1 分层验证

1. **单元测试**（test/mir/，仿现有 check-mir-*）：
   - 区间构造：手写 MFn（phi/循环/跨块 use），断言 `start/end` 正确。
   - slot 打包：混合 4/8 字节值，断言布局与 QBE spill 输出一致。
   - 并行移动：构造移动环/链，断言展开后无冲突。
2. **Oracle asm-diff**（核心）：
   - 用现有 P0 基线（/tmp/mir-backend-base/asm/*.s，bridge 冻结产物）
     作 oracle。
   - `MCC_MIR_BACKEND=1` 使能 MIR 后端输出；对同一测试源分别跑
     bridge 路径（默认）与 MIR 路径（MCC_MIR_BACKEND=1 +
     MCC_MIR_BACKEND_REGALLOC=1），diff 汇编。
   - **语义等价判定**：asm 文本可不等（指令顺序/寄存器名不同），但
     **功能必须一致**——对每个测试 `.c` 用两条路径分别编译成可执行
     文件并运行，比对退出码/输出（与 collect.sh 的 gen/ 目录做法一致）。
     这比 asm-diff 文本比对更稳健，asm 文本比对仅作诊断提示。
3. **自举回归**（最终门）：
   - `make check-sysroot-static`（完整自举：mcc 编译 mcc → 自举 mcc →
     编译 hello 运行）。
   - 用自举产物编译 `src/parse/expr_binary.c -O2`（历史崩点，见
     a270435）确认无段错误。
   - 全量 test/c99|c11|abi + community 回归。

### 4.2 增量落地（规避大爆炸）

- Phase A：`mreg_liveness + mreg_intervals`，dump 区间与 filllive 对照
  （`debug` 开关，输出格式仿 printfn）。
- Phase B：`mreg_slots`（纯槽分配，不做寄存器），与 spill.c 的 slot 布局
  对照（同一函数两路径 dump `fn->slot`）。
- Phase C：`mreg_scan`（寄存器分配 + 指令改写），asm-diff 语义验证。
- Phase D：`mreg_phiresolve`（边拷贝），开始整体 asm-diff。
- Phase E：开关接管——`MCC_MIR_BACKEND=1` 时完全跳过 bridge/LIR，
  直接 MFnM→emit，跑自举。

### 4.3 风险与缓解

| 风险 | 缓解 |
|:-----|:-----|
| 线性扫描在深嵌套循环 spill 过多 | cost 加权 + 跨回边区间延伸；备选区间着色 |
| caller/callee-saved 跨调用处理遗漏 | 单测覆盖跨调用活跃值；与 QBE `nrsave` 预算对照 |
| phi 边拷贝与 slot 混用冲突 | pmgen 并行移动保证；slot 与寄存器共存时先读后写 |
| emit 层 prologue/epilogue 未保存 callee-saved | `fm->regsused` 输出 + 单测断言 |
| 与 P2 ABI lowering 的寄存器占位交互 | MV_REG 区间只读占位，分配器不覆盖已占用位 |

---

## 5. 文件落点（P4 实施期）

```
src/mir/regalloc.c     — mreg_liveness/intervals/slots/scan/phiresolve
include/mir.h          — 扩 MFnM（intervals 数组、regsused 已存在）
src/target/x86_64/x86_64_mbe.c — MCC_MIR_BACKEND=1 时串联 regalloc+emit
test/mir/regalloc_test.c        — 单测
```

（本文档只做设计，P4 实施时按此方案开发，不在此次修改源码。）
