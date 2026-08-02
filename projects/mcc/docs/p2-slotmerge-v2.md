# P2 二期：slotmerge 安全重启用方案（缺陷 J 修复设计）

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 状态：**调研完成，方案设计定稿，未实施**。
> 目标：修复缺陷 J（slotmerge 破坏自举误编译），给出可安全重新挂载的二期方案。
> 前置文档：`docs/p2-spill-slot-reuse.md`（P1/一期设计）、`.issues/0802.md` 缺陷 J 段。
> 约束：本方案只读 `src/opt/`、`src/ir/`、`src/emit/`/`src/target/`，不改业务代码，仅新增本文档。

## 1. 缺陷 J 根因（读码结论）

### 1.1 复现链（0802 仲裁确认）

- f22f3d7 挂载 slotmerge 后：自举 mcc 编译任意 C 文件 SIGSEGV（含空文件 `-E`）；object-level bisect 定位
  **token.c error 函数（1332 slots）、spill.c spill 函数（186 slots）**损坏。
- 禁用（97c8541）后 verify-all 6/6。实验树 /tmp/selfK + repro2 + pp10.s/tok10.s。
- 观测症状：自举 mcc 在 `main` 崩溃（`mov -0x72c(%rbp)` page fault），worker-test 复现 rbp 破坏。
- worker-cpp 排查结论：R1=fn->slot 缩小未覆盖 RMem offset 范围（含负偏移）；R2=重编号后
  "RMem base+offset 与压紧后新布局语义冲突（物理帧内、逻辑共享）"。

### 1.2 关键机制（代码证据）

**槽位地址由 fn->slot 决定，压缩即整体平移。**
`x86_64_emit.c:185-208` `slot(r, e)`：正 id 的槽位地址为 `-4*(fn->slot - s)`（rbp 基）。
因此 `fn->slot` 从 N 缩到 M 时，**所有**槽位物理位置同时上移 `4*(N-M)` 字节。
RMem base=slot 时 emit（`x86_64_emit.c:347-355`）把 `slot(base)` 折入 `m->offset`，
最终寻址为 `rbp + slot(s) + offset + index*scale`——**RMem 的访问范围是"槽位地址 ± 偏移"的字节区间，
不是该槽自己的 4/8B**。

**front_slot 划分两区**：`spill.c:439-440` `fn->front_slot = locs`（spill 前前端已分配槽数）。
前端局部变量 id ∈ [0, front_slot)，spill/rega 动态槽 id ∈ [front_slot, nsl)。
RMem base=slot 的来源：`x86_64_isel.c:882-887` `seladdr` 把带槽位的 base temp 改写为 `SLOT(s)`
（前端局部数组/结构体的 base，id < front_slot）。

**slotmerge 的存储模型只看 RSlot 访问位点**：`slotmerge.c` 的 `TOUCHMEM`（L115-147）/`TOUCH`（L110-114）
按指令流中的 RSlot/RMem 引用记录"该槽被访问过"，合并与重编号全部以此为准。
模型与实际存储范围的偏差，就是缺陷 J 的全部来源。

### 1.3 三个具体缺陷

#### C1：RMem 字节范围未被压缩帧覆盖 → 越界写（崩溃）

`TOUCHMEM` 的 ext 模型低估 RMem reach：

- **负偏移从不计入 ext**：`ACC`（L98-109）仅当 `sz > ext[s]` 才更新，`ext[s]` 初值 0；
  静态偏移 `_off = m->offset.bits.i` 为负时 `sz = _off + clssz < 0` → ext 保持 0 → `units = 1`。
- **动态 index 的保留是猜测**：`dyn[s]` 时 `units = front_slot - s`（L258-260）；
  若 base 是 spill slot（s ≥ front_slot，rega 后 RMem base 可为 spill 槽，见 `rega.c:565-572`
  对 `m->base` 的 `ralloc`），`rem = front_slot - s ≤ 0` → 退化为 1 单元。
- **正向 reach 顶越出 rbp**：保留仅推 `cur`，若某 RMem 的正偏移 range 顶 `4s + max_off + size > 4*cur`，
  访问越过 rbp → **写坏 saved rbp / 返回地址** → SIGSEGV（观测到的 rbp 破坏即此）。

结果：RMem store 写坏 callee-saved rbp → 二次崩溃 `mov -0x72c(%rbp)`；或写坏调用者帧。

#### C2：别名覆盖 → 值错（自举误编译的直接元凶）

**聚合对象的完整字节范围可以完全不出现在 RSlot 位点**：

- `x86_64_isel.c:115-121`：取局部地址时 `emit(Oaddr, Kl, r1, SLOT(s), R)`——聚合 base 槽只被
  "单点访问"（rng 区间跨度 = 1，且 sz=0）。
- 经 `p = a; p[i]` 路径：地址物化进寄存器后，后续访问的 RMem base 是 **RTmp**（寄存器），
  slotmerge 完全看不到聚合的字节范围。

这样的聚合 base 槽在 slotmerge 中：单点访问不参与合并（`rng[s].b - rng[s].a > 1` 不满足），
但**仍会被重编号移动**（`map[s] == -1` 分支，L269-273，`newid[s] = cur` 只给 1 单元）。
聚合实际 k 单元（如 `int a[100]` 的 400B）被压到 1 单元位置 → 相邻合并 slot 的数据被
经指针/RMem 的写入**别名覆盖** → 值错。这是 token.c/spill.c 大函数上"引用都在帧内但运行值错"
（worker-cpp R2 描述）的直接机制。

#### C3：已物化地址陈旧

若局部地址已被折叠进其它指令或外泄（全局、参数），压缩后指向旧物理位置。
Oaddr 指令本身的 arg 会被 REWRITE 覆盖（L289-312），但**寄存器中已计算的地址不可追踪**。

### 1.4 一句话根因

> slotmerge 以"RSlot 访问位点"作为槽存储范围的唯一依据，而栈上实际存储范围由两类
> **位点不可见**的因素决定：RMem 的"基址 ± 偏移/动态 index"字节区间（非 4/8B 粒度），
> 以及经 Oaddr/寄存器间接访问的前端聚合对象（数组/结构体）的完整字节范围。
> 压紧/重编号把其它槽移入这些字节区间（C2 值错）或使区间越出压缩帧（C1 崩溃）。

## 2. 二期方案（分层）

### 2.0 核心原则：frontend 区与 spill 区分离

- **frontend 区 [0, front_slot) 绝不移动、绝不合并、绝不压紧**：
  前端布局保证该区所有 RMem reach 落在 [0, front_slot)（前端正确性前提），
  区不变 → reach 不变 → C3 地址陈旧类问题整体消除。
- **只在 spill/动态区 [front_slot, nsl) 内做生命周期合并与压紧重编号**。
- 合并后新 `fn->slot = max(front_slot, spill 区压紧结果)`，恒有 `fn->slot >= front_slot`。

### 2.1 Layer 0 — 安全重启用前置（正确性基线）

1. RMem base slot（无论 base 在 frontend 区还是 spill 区）一律 `mergeable = 0`，
   保持原 id，不参与合并（现状已如此，保留）。
2. **只有满足"所有访问位点均为直接 RSlot、且字节范围 == 槽宽"的 spill slot** 才可合并：
   - 排除 Oaddr 单点访问的槽（其真实范围未知，按 unmergeable + 前端声明大小保留）；
   - 排除经寄存器间接/动态 index 的槽（范围不可证）。
3. frontend 区所有槽保持原 id，`fn->slot` 的缩减只来自 spill 区压紧。

### 2.2 Layer 1 — 精确字节 reach 模型（替换现有 ext 计算）

对每个被引用槽记录字节区间 [lo, hi]（相对该槽地址）：

| 访问形式 | lo | hi |
|:--|:--|:--|
| 直接 RSlot | 0 | 槽宽（4/8B） |
| RMem base，静态 | `\|min_off\|`（**含负偏移**） | `max_off + clssz`（含 `index` 常数×scale） |
| RMem base，动态 index | 0（不可证下界） | `nsl - s`（保守保留至帧顶） |
| Oaddr/聚合 | 前端声明字节大小（向下取整到槽单位） | 同左 |

修正要点（相对 f22f3d7）：

1. **ext 改记"两向 max"**：`ext[s] = max(ext[s], max_off + clssz, |min_off| + clssz)`，
   负偏移不再被忽略（修 R1）。
2. **动态 index 的保留改为 `nsl - s`**（整段剩余帧），不再用 `front_slot - s`（修 spill base 退化）。
3. **Oaddr/聚合槽**：从前端取声明大小（本期可先用保守值：聚合槽一律 unmergeable + 保留至 front_slot）。
4. 保留现状的循环保守规则（`b->loop > 1` 内槽不合并）与单点不合并规则。

**压紧范围**：unmergeable 槽保持原 id 并按 reach 保留 `units`；合并槽（全为 spill 槽，id ≥ front_slot）
的新 id 从 `max(front_slot, 所有 unmergeable 槽的 s+units)` 起分配，向上压紧。
`fn->slot = 压紧后最大 id + 1`。

### 2.3 Layer 2 — emit 防御性边界检查（静默误编译 → 响亮失败）

采纳 0802 里 worker-test 的 P2-三期建议，前移到本方案强制实施：

- emit 时对每个 RMem 验证字节区间 `[slot_addr(s) + min_off, slot_addr(s) + max_off + size]`：
  1. ⊆ `[rbp - 4*fn->slot, rbp]`（不越帧）；
  2. 不与任何**其它被引用槽**的存储区重叠（对碰 C2 别名覆盖）。
- 任一违规 → 报错并**该函数回退为不启用 slotmerge**（不静默产出错误代码）。
- 新增 `-dP` dump：逐槽打印 id/width/reach/merge 映射，供自举前人工核对。

### 2.4 Layer 3 — 性能恢复路径（后续可选）

- v2 首版预期：BZ2 恢复到 ~1000B 区间（spill 区合并的收益取决于 `front_slot` 占比，
  需实测；984 slots 中若 spill 区占大头则收益显著）。
- 后续放开项（每项以 Layer 2 检查为兜底，逐步放宽）：
  1. 用"循环闭包扩展 live 区域"（docs/p2-spill-slot-reuse.md §3.3）替代"循环全禁"；
  2. frontend 区内部静态 ext 槽的块内区间合并（需前端提供精确聚合声明大小）。

## 3. 实施步骤

1. 在 `slotmerge.c` 上改造（或新建 `slotmerge_v2.c`）：
   - `TOUCHMEM` 的 reach 收集改两向 ext + `nsl - s` 动态保留；
   - 合并候选加"全位点直接 RSlot 且字节范围 == 槽宽"过滤；
   - 重编号限定在 [front_slot, nsl) 内，frontend 区 id 不动。
2. `passes.c` 重新挂载：`P(spill); P(rega); if(ol>=1) P(postra); P(slotmerge);`（位置与 f22f3d7 相同）。
3. emit 防御检查：`x86_64_emit.c` 的 RMem 路径加边界/重叠验证（其余 target 同步或至少报未实现）。
4. `-dP` dump 与回归门禁脚本（复用 /tmp/selfK 实验树的复现路径）。

## 4. 验收标准

1. **verify-all 6/6**（含 `check-sysroot-static` 完整自举：自举 mcc 编译 hello + 空文件 `-E`，串行执行）。
2. **BZ2_decompress 栈帧 < 1000B**（或至少显著低于 2152B），且 bzip2 解压输出与 gcc 版逐字节一致。
3. **-dP dump 无 reach 重叠**；emit 防御检查在完整自举中**零触发**。
4. **开关 A/B**：除 BZ2 外测试语料（check-c99/c11/cpp）在启用/禁用两态下产物一致。
5. **多目标冒烟**：x86_64 全量；arm/aarch64/i386 至少编译并运行小程序（slot 语义对所有 target
   一致，但 i386 无 Kl 寄存器需重点看 8B 槽）。

## 5. 风险清单

| 风险 | 说明 | 缓解 |
|:--|:--|:--|
| 收益不足 | spill 区-only 合并若 `front_slot` 占大头，BZ2 可能达不到 <1000B | 首版先量化 `front_slot`；不足则 Layer 3 放开 frontend 区静态 ext 合并（有防御检查兜底） |
| RMem base 为 spill 槽 | rega 后 base temp 被 spill 时出现（`rega.c:565-572`） | reach 保留 + Layer 2 检查；语义上该类访问原布局已受限 |
| 循环跨迭代误判 | 线性位置模型无法证明循环内两槽不重叠 | 维持"循环内不合并"保守规则（现状），后续再放开 |
| i386 Kl 槽 | 8B 槽占 2 单元，与 4B 槽不可互并 | 现状已按 width 分组，保留 |
| 与 postra 顺序 | postra 按 slot id 追踪 Ocopy | slotmerge 保持在 postra 之后（与 f22f3d7 一致） |
| 自举门禁并发竞态 | check-sysroot-static 写 /tmp 并重建 sysroot，并发会互相覆盖（0802 教训） | 回归验证独立干净 checkout 串行执行 |
| emit 防御检查开销 | 逐 RMem 边界/重叠验证有少量编译期开销 | 仅 emit 期一次扫描，O(n*m) 可接受；超标可改为抽样 |

## 6. 参考

- 本缺陷复现与中间结论：`.issues/0802.md` 缺陷 J 段（L502-565）。
- 一期设计：`docs/p2-spill-slot-reuse.md`（§3 方案、§5 正确性约束、§6 实施步骤）。
- 代码：`src/opt/slotmerge.c`（f22f3d7 版，当前禁用）、`src/opt/spill.c:149-180`（slot()）、
  `src/opt/spill.c:439-440`（front_slot）、`src/opt/rega.c:86-129`（fn_local_slot/rref）、
  `src/opt/rega.c:565-572`（RMem base ralloc）、`src/target/x86_64/x86_64_emit.c:185-208`（slot()）、
  `src/target/x86_64/x86_64_emit.c:347-371`（RMem emit）、`src/target/x86_64/x86_64_isel.c:115-121`（Oaddr）、
  `src/target/x86_64/x86_64_isel.c:882-887`（seladdr base→SLOT）。
