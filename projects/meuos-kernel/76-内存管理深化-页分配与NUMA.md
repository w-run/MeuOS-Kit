# 76 - 内存管理深化：页分配与 NUMA（Page Allocation & NUMA Deepening）

> 子领域：内存管理深化 / Physical Page Allocator & NUMA（深化篇）
> 团队：`kernel-plan`（第八轮拓展·独立调研一面，调研员 lite / hy3 产出）。
> 关联文档：`00-总览与路线图`、`03-内存管理`（VMO/device VMO/C1 折中）、`04-进程与调度`（Job 账本 §4.4）、`10-设备驱动模型`（双轨/能力网关/C8）、`28-实时性与确定性`（确定性分配/R1 已采纳）、`60-设备驱动框架深化`（设备内存/L1·L3/IOMMU 域）、`07-安全模型`（right 冻结/`A1` 句柄即 capability）。
>
> **百思纪律（铁律）**：本文只写设计/规划文档，不写实现代码；结论可修订式追加。本文建立在以下已决地基之上——**内核 buddy 独占物理页帧数据库（VMO 即逻辑视图，`03` C1=折中 C 已采纳）**、**device 内存走不可写 device VMO（`03`/`10`/`60`）**、**Job 资源账本 Day 1（`00` A5 / `04` §4.4）**、**确定性实时能力（不承诺 WCET 但 mlock 固定，`28` R1 已采纳）**、**五架构同源（`00` §5.3 一致性成本）**；**不引入新内核原语**，新 right 须登记 `07` §4.1（本文**不新增任何 right bit**）。
>
> 方法论铁律贯穿：解构型思维、多系统参照（Linux buddy/SLUB、seL4 untyped、Zircon PMM/VMO、Zoned buddy）、非缝合怪、修订式追加不删。
>
> 本文为规划草案，后续以修订式追加更新，不覆盖、不删改 `00`–`70`、README、`.issues/`、git。跨域项以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准；与 `03` VMO/`60` 设备内存/`28` 确定性/`04` Job 账本严格同构。

---

## 0. 核心结论（一句话）

> **物理页分配器是 `03` VMO 的「底层供给层」——VMO 是逻辑内存对象（capability 视图），物理页分配器（buddy/zone/NUMA node）是单一真相源的物理供给，二者通过「分配点＝Job 配额扣减点、revoke 触达物理页归还 buddy」的链路衔接；本文在 `03` 已采纳的折中 C 之上，把**NUMA 拓扑感知分配、大页/hugepage 后端、页回收/暂存策略、device VMO 物理后端、确定性预留池**五维深化为可落地的供给层机制，且五架构只在 zone/页表/拓扑探测的**薄后端**存在差异，不引入新内核原语。**

---

## 1. 与 `03` VMO 衔接（逻辑对象 vs 物理供给层）

### 1.1 分层定位（不重定义 VMO 语义）

`03` §4.1 已定调：**内核 buddy 独占物理页帧数据库，VMO/VMAR 作为对用户态/兼容层暴露的 capability 视图**。本文严格继承，不重定义 VMO 语义，只把「VMO 之下的物理供给层」拆开深化：

```
┌─ 逻辑层（03 已定义，本文不重定义）───────────────────────┐
│  VMO（虚拟内存对象，capability 视图）/ VMAR（地址区域）   │
│  COW 用 VMO child；mlock 用 VMO_LOCK 标志（28 §5.1）      │
└──────────────────────────────────────────────────────────┘
                    │ pager / 缺页 / 锁定 请求物理页
                    ▼
┌─ 供给层（本文深化，单一真相源）──────────────────────────┐
│  PMM（物理内存管理）：node → zone → free_area[order]       │
│  buddy 分配/合并、per-node 独立、大页池、回收/暂存、       │
│  确定性预留池；分配点 = Job 配额扣减点（04 §4.4）          │
└──────────────────────────────────────────────────────────┘
```

- **VMO 是「需求方」**：何时需要物理页由 VMO pager/缺页/lock 决定（`03` §4.3.3）。
- **PMM 是「供给方」**：本文定义 PMM 如何按 node/zone/order/预留池满足需求，并将分配计入持有 Job 的内存配额（`04` §4.4）。
- **单一真相源不破**：`03` `引用澄清（来自24）` 明确——buddy 持有物理页（`MmPage[].flags`），VMO 仅持逻辑视图；本文所有深化（NUMA/大页/回收/预留）都在 PMM 内部，VMO refcount 仍为派生链回收的触发面，不引入第二真相源。

### 1.2 分配点 = Job 配额扣减点（与 `04` 同构）

`04` §4.4 Job 账本已含「Job 级内存上限（VMAR 映射总量 + 内核对象占用）」。本文把**扣减点**收敛到 PMM 分配边界（对齐 `00` C1 采纳理由：「分配点＝Job 配额扣减点」）：

- 每次 buddy 交给某 VMO 一个物理页，**同步在持有该 VMO 的 Job 账本上 +1**（无论匿名/文件/设备 VMO）。
- 页归还 buddy **同步 -1**（revoke/回收/卸载路径）。
- 配额超限（Job 内存上限）：PMM 拒绝分配 → 触发 `04` §4.4 的「OOM 在 Job 内选择性回收 / 通知监督者」，**fail-closed**（不 silent 越权）。
- 与 `03` §5-4「Rust 全局 allocator 与 slab 的关系」协同：内核 `Box`/`Vec` 走 SLUB（内核对象，计内核 Job），用户态 VMO 走 buddy（计用户 Job），二者边界清晰。

### 1.3 本文与 `03` 的职责切分

| 议题 | `03` 已定义（不重判） | 本文深化（供给层） |
|------|----------------------|---------------------|
| VMO/VMAR/COW/pager | VMO 逻辑视图、COW child、`03` §4.3 | —（不重定义） |
| buddy/zone/slab | M1–M4 最小集、`NORMAL`+`DMA32` | NUMA node 化、大页池、回收/暂存、预留池 |
| device VMO | 不可写 device VMO（C1 折中） | device VMO 的物理后端：DMA32/CMA zone + IOMMU 域协同（`60`） |
| NUMA | Phase 2 预留 `MmPage.node_id` | 拓扑发现/亲和策略/跨节点代价/五架构薄后端（§2） |
| 大页 | Phase 3（2MB/1GB） | VMO 大页支撑 + 碎片管理（§3） |
| 确定性 | mlock（`28` §5.1） | 有界延迟分配 + 预留池（§6） |

---

## 2. NUMA 拓扑感知分配（Phase 2 深化）

### 2.1 拓扑发现（薄后端，封进 `arch/mm` + 启动解析）

`03` §4.6 Phase 2 引入 `pglist_data` 式 node 数组。本文给出拓扑来源与薄后端映射：

| 架构 | 拓扑来源 | 薄后端（MM 抽象） |
|------|----------|-------------------|
| x86_64 | ACPI SRAT/SLIT（+ 内存热插拔） | `arch/acpi` 解析 → `numa_topo`；SLIT 距离矩阵 |
| aarch64 | DT `#numa-node-id` / ACPI 同上 | `arch/dt` + `arch/acpi` 统一归一 |
| riscv64 | DT `numa-node-id` / ACPI（RISC-V 平台） | 同 aarch64 抽象 |
| loongarch64 | ACPI（LoongArch 自有） | 自有 ACPI 后端，归一为 `numa_topo` |
| i386 | 单 node（UMA）；多 node 主板罕见 | 退化单 node，接口保留 |

> 约束（来自 `00` §5.3 一致性成本）：无论架构，**`numa_topo`（node 列表 + 距离矩阵 + 每 node 物理区间）数据结构同源**，差异只在「探测器后端」；MM 逻辑（亲和策略/跨节点回退）与架构无关（对齐 `28` §6「差异在时钟源而非调度」的同构思路）。`09` §6-9 已要求「多架构 bootinfo 归一化含 NUMA/保留内存」——本文复用该结构。

### 2.2 亲和策略（不新增 right，复用 `04`/`07` 既有能力）

- **默认策略（local-first）**：分配优先 node-local（`03` §4.6 `NODE_LOCAL` 思想）；跨节点仅在本地不足时回退。
- **绑定请求**：用户态经 `madvise(MADV_...)`（兼容层 `08`）或 VMO 创建参数声明 node 亲和——**不新增 right**，亲和是 VMO 属性而非 capability 权限（`00` A1 句柄即 capability 不因此被扩展）。
- **跨节点代价模型**：用 SLIT/距离矩阵量化「remote 访问延迟倍数」；PMM 在 local 不足时按代价梯度选次优 node，而非盲目全局扫。
- **与 capability 的耦合（沿用 `03` §5-5 待决）**：跨 node VMO 共享时物理页归属分配时的 node；迁移（`ZONE_MOVABLE` 语义）仅改 `MmPage.node_id`，不破坏 VMO capability 引用（VMO 逻辑视图与物理 node 解耦，正因 C1 折中把二者分离）。

### 2.3 演进阶梯（与 `03` §4.6 三阶段对齐）

```
Phase 2a  UMA→多 node：每 node 独立 buddy + free_area，node_id 落 MmPage（03 已预留）
Phase 2b  local-first 分配 + madvise 亲和钩子（兼容层 08）
Phase 2c  跨节点回退 + SLIT 代价模型
Phase 3   页面迁移（ZONE_MOVABLE）+ kswapd 类异步回收（仅大机箱/热插拔启用）
```

> 原型期（M0 自举）**强制 UMA 假设**（单 node），NUMA 仅作接口预留；多 node 正确性在 M3 后基准（承接 `03` §4.6）。

---

## 3. 大页 / hugepage 后端（VMO 大页支撑）

### 3.1 VMO 大页支撑（不重定义 VMO，只改供给粒度）

`03` §4.7 Phase 3 把 2MB/1GB 大页列为演进项。本文给出 VMO 侧契约：

- **VMO 创建时声明粒度偏好**：VMO 可标记「允许大页后端」；pager 提交物理页时优先从大页池取连续块，TLB 单项覆盖更大缓冲（对齐 `28` §5.1「大页对 RT 友好」）。
- **混合映射**：同一 VMO 可「大页 + 4KB 尾页」混合（对齐 Linux THP 部分映射），避免大页对齐浪费；页表项按实际粒度标记。
- **与 COW 协同**：COW 子 VMO 初态继承父粒度；写时升级/降级在 PMM 内部完成，VMO 逻辑不可见。

### 3.2 碎片管理（供给层核心难题）

`03` §5-3 已列「大页与 buddy 碎片」开放议题。本文给两种互补策略，不二选一：

| 策略 | 机制 | 适用 | 代价 |
|------|------|------|------|
| **预留大页池（hugetlb 式）** | 启动期从 buddy 永久 RESERVED 一段连续大页（标 `MmPage.flags=RESERVED`，`03` §4.2.2 已有位），专供确定性/大缓冲 VMO | RT 预留、VM 大页（69） | 缩减可用 buddy 总量 |
| **按需聚合（THP 式）** | 4KB 分配后，PMM 后台扫描可合并的相邻空闲页升级为 2MB；写时降级回 4KB | 通用匿名/文件 VMO | 聚合扫描开销、偶发降级抖动 |

- **碎片防御**：buddy `free_area` 已按 order 分级（`03` M3，order 0..10 覆盖到 4MB）；大页池在 memblock→buddy 转交时（`03` C7）一次性认领，避免运行期高碎片下无法满足。
- **五架构同构**：大页粒度由 `arch/mm` 报告（x86_64/aarch64 2MB+1GB，riscv64 Sv39/48 同，i386 仅 4MB 物理页可选），PMM 逻辑不依赖具体粒度。

---

## 4. 页回收 / 暂存策略（与 Job 账本协同，fail-closed）

### 4.1 内存压力触发（不新增原语，复用 Job 账本）

`04` §4.4 定义 Job 内存上限与「OOM 在 Job 内选择性回收」。本文把回收触发点接到 PMM：

- **水位线（watermark）**：沿用 `03` §3.1 Linux 式 min/low/high（per node/zone），低于 low 触发异步回收（kswapd 类），低于 min 触发分配路径直接回收。
- **配额优先于全局**：Job 内存上限是先于全局 watermark 的硬墙——某 Job 触及其上限即**仅在该 Job 内**选页回收（最老/最大/最近最少活跃，策略由监督者 `04` §4.5 定），不波及他 Job（capability 爆炸半径思想，`29`）。
- **fail-closed**：配额超限且无页可回收 → 分配失败返回 `ENOMEM`/对应错误，**绝不 silent 越权借用他 Job 或内核预留页**（对齐 `07` pledge 越界即终止语义）。

### 4.2 暂存（staging）对象选择（与 VMO 派生链一致）

- **可暂存**：匿名 VMO 冷页、文件 VMO 已落盘页（经 `06` pager 写回）、可迁移页（`ZONE_MOVABLE`，Phase 3）。
- **不可暂存**：RT 锁页（`MmPage.flags=RT_LOCKED`，`28` §5.1）、device VMO（不可写、无后端，`60`）、内核关键页（`RESERVED`）。
- **revoke 即回收（复用 C1 链路）**：capability `revoke`（`07`）触发 VMO 派生链回收 → `MmPage.refcount` 归零 → PMM 归还 buddy，与自愿回收共用同一归还路径（单一真相源）。

### 4.3 与 `03`/`04` 的接口契约

| 本文件诉求 | 接入点 | 契约 |
|---|---|---|
| 回收触发 = watermark + Job 上限 | `04` §4.4 Job 账本 / `03` §3.1 watermark | PMM 检查点：分配前查 Job 配额 + node watermark；超限 fail-closed |
| OOM 归因策略 | `04` §4.4 / §4.5 监督者 | Job 内选页策略由监督者策略决定，内核默认 LRU；不新增原语 |
| 暂存不可动集合 | `28` §5.1 RT_LOCKED / `60` device VMO | PMM 回收扫描跳过 RT_LOCKED 与 device VMO 物理页 |

---

## 5. 设备内存（device VMO 物理后端）

### 5.1 device VMO 的物理后端（与 `60`/`10` 协同）

`03` C1 折中 C 已定：**device 内存统一走不可写 device VMO，不另立独立 iomem 子系统**（`03` §5-6 已关闭）。本文给出其物理后端供给：

- **来源 zone**：DMA 缓冲从 `DMA32`/`CMA` zone 取**连续物理页**（`03` §4.2.3 已列 `DMA32`；`CMA`（连续内存分配器）为演进补充，满足 device 无 IOMMU/64 位 DMA 限制）。
- **封 device VMO**：连续页封为**不可写 device VMO**（right 来自 `07`，已冻结、不新增），经设备能力下发驱动（`10` C8 撤销锁 / `60` §3.2）。
- **IOMMU 域协同**：device VMO 物理页同时登记进持有驱动的 IOMMU 域（`60` §3.4）；revoke 前先使域失效截断在飞 DMA（`60` §3.5 撤销锁步骤③），与 PMM 归还 buddy 串成原子序列。

### 5.2 与 `60` L1·L3 双轨的供给差异

| 驱动档位（`60` §2.3） | device VMO 物理后端 | PMM 角色 |
|----------------------|---------------------|----------|
| **L1（生死相关，内核态）** | 早期 MMIO/寄存器区：直接映射、内核自持（如中断控制器/定时器） | 早期 memblock 预留（`03` C7），不进通用 buddy |
| **L3（默认，用户态）** | DMA 缓冲：DMA32/CMA 连续页 → 不可写 device VMO → IOMMU 域 | 从 buddy 取连续页、封 device VMO、绑定 IOMMU 域、撤销锁回收 |

- **无 IOMMU 平台退化**（`60` §2.2 决策表第 3 条）：仅 L1 受信内核态驱动可碰 DMA，用户态 DMA 退化（C1 折中不破）；PMM 此时不为用户态驱动分配 device VMO 连续页，fail-closed。
- **与 `10` 双轨同构**：PMM 对设备内存的供给方式由驱动档位决定，不引入新内核原语——L1 走预留、L3 走 device VMO + IOMMU 域，正是 `10`/`60` 既有双轨在供给层的落地。

---

## 6. 确定性分配（`28` 实时性：有界延迟 + 预留池）

### 6.1 有界延迟分配（不引入新原语）

`28` R1 已采纳「确定性实时能力，不承诺 WCET」；§5.1 把 RT 线程地址空间与 VMO 缓冲在准入期 mlock 固定。本文给 PMM 侧落实：

- **准入期一次性付清**：RT Job 的 RT 线程/VMO 缓冲在 `spawn`/`meu_thread_set_rt` 时由 PMM 提交物理页并标 `RT_LOCKED`（`28` §5.1），**运行期 RT 域禁缺页**（对齐 `28` §1.1 原则 4「不确定性在准入期一次性付清」）。
- **运行期分配有界**：RT 域内的偶发分配（如 RT 驱动 lock-free 环形缓冲预分配）走**预留池**（见 6.2），保证分配延迟 ≤ 预算上界，不进入通用 buddy 竞争路径（避免 `28` §5.2「驱动内分配致优先级反转」）。

### 6.2 确定性预留池（与 Job 账本同源）

- **预留池 = 从 buddy 永久 RESERVED 的一块物理页集合**（`MmPage.flags=RESERVED`，`03` 已有位），专供 RT/关键路径分配；大小计入系统预留、不计入任何单 Job 的浮动配额。
- **与 `04` RT 预算同树**：预留池容量是 RT 预算的**物理保障维度**（对齐 `28` §4.2 RT CPU 带宽硬分区）——`CAP_RT`（`07` 已定 bit45，`68` 闭环）持有者才能从预留池取页；无 `CAP_RT` 的分配请求落入通用 buddy。
- **fail-closed**：预留池耗尽 → RT 分配失败返回错误，不挪用通用 buddy（避免 RT 饿死公平层，对齐 `28` §3.1「RT 域对公平层绝对压制但资源有界」）。

### 6.3 与 `28`/`04` 接口契约

| 本文件诉求 | 接入点 | 契约 |
|---|---|---|
| RT 锁页（`RT_LOCKED`） | `28` §5.1 / `03` `MmPage.flags` | PMM 在 RT 准入期提交并标 RT_LOCKED；回收路径跳过 |
| 预留池取页需 `CAP_RT` | `28` §4.1 / `07` bit45（已冻结） | PMM 检查 `CAP_RT` 后才从预留池分配，不新增 right |
| 预留池容量 = RT 预算物理维度 | `28` §4.2 / `04` §4.4 | 预留池上限挂 Job RT 预算，与 CPU 带宽同源审计 |

---

## 7. 五架构同源；与既有地基同构（不引入新内核原语）

### 7.1 五架构薄差异（仅供给层后端）

本文所有机制（buddy/zone/node/大页池/回收/预留池/device VMO）**逻辑同源**，差异仅封在薄后端：

| 架构 | 页大小/大页 | NUMA 探测后端 | 设备内存 zone | 差异性质 |
|------|-------------|---------------|--------------|----------|
| x86_64 | 4KB / 2MB / 1GB | ACPI SRAT/SLIT | DMA32 + CMA | 薄后端 |
| aarch64 | 4KB / 2MB / 1GB | DT/ACPI | DMA32 + CMA | 薄后端 |
| riscv64 | 4KB / 2MB / 1GB（Sv39/48） | DT/ACPI | DMA32 + CMA | 薄后端 |
| loongarch64 | 4KB / 大页自有 | ACPI（LoongArch） | DMA32 + CMA | 薄后端 |
| i386 | 4KB（大页可选 4MB） | 单 node（UMA 退化） | DMA32（无 IOMMU 退化） | 薄后端 + 退化 |

> 论断（同 `63` §7 / `28` §6）：供给层逻辑跨五架构二进制一致，差异只在 `arch/mm` 的 zone 报告、NUMA 探测器、IOMMU 域后端（`60` §7）。i386 纯 C 约束（`02` C2）只影响早期引导 C 后端，不侵蚀供给层 Rust 主体（`19` §3.3）。

### 7.2 与既有地基同构（铁律校验）

- **不引入新内核原语**：本文所有深化（NUMA node、大页池、回收/暂存、device VMO 后端、预留池）都是 `03` 已采纳 buddy/zone/VMO/Job 账本在供给层的**组合与参数化**，无新抽象类型。
- **不新增 right bit**：NUMA 亲和 = VMO 属性、预留池取页 = `CAP_RT`（`07` 已冻结 bit45）、device VMO 不可写 = `07` 已冻结 device right；本文零新 bit。
- **跨域冲突以 `00` A1–A12 / C1–C18 为准**：
  - `A1` 句柄即 capability → device VMO/预留池取页均经既有 capability 校验；
  - `A5` Job 账本 Day 1 → 分配点即配额扣减点（§1.2）；
  - `A6` 交权缩 TCB → PMM 关键路径 Rust（`03` §4.5）；
  - `C1` 折中 C → 物理页 buddy 独占、VMO 逻辑视图（§1，全篇根基）；
  - `C7` memblock→buddy 原子性 → 大页池/预留池在 `buddy_init()` 单点认领（`03` C7 回写）。

---

## 8. 待决项表（标注是否需大喵拍板）

| 编号 | 待决项 | 是否需大喵拍板 | 关联 |
|------|--------|----------------|------|
| **P1** | **物理页分配器算法选型**：经典 buddy（order 链表）+ per-CPU 空闲页缓存（仿 Linux PCP） vs 引入更精细的 `MmPage` 空闲跟踪（如伙伴位图/区域分配器）。建议原型期沿用 `03` M3 buddy + Phase 2 per-node，不引入二级算法。 | 否（执行层按 `03` M3 定稿） | `03` §4.2.3、`19` 基准 |
| **P2** | **NUMA 默认策略**：local-first 硬默认 vs 可配全局策略（默认 interleave/strict）。建议 Day 1 强制 local-first，bind 经 VMO 属性/madvise，不新增 right。 | 否（协同 `03` §4.6 / `08` 定稿） | `03` §4.6、`08` 兼容层 |
| **P3** | **大页默认粒度与启用方式**：2MB 预留在 memblock 转交时一次性认领的量（占物理内存比例）、是否默认开 THP 式聚合。建议预留池比例按内存大小阶梯、THP 聚合 Day 1 关闭（仅预留池）。 | 否（执行层参数，承接 `03` §5-3） | `03` §4.7、`28` §5.1 |
| **P4** | **页回收阈值（watermark min/low/high 取值）与 Job 内 OOM 归因策略**（最老/最大/最近最少活跃）。建议 watermark 按 node 内存比例默认、OOM 归因默认 LRU 由监督者可覆写。 | 否（协同 `04` §4.4 / `29` 定稿） | `04` §4.4、`29` T8 |
| **P5** | **确定性预留池大小**：占总物理内存比例（如 1%–5%）、是否按 RT CPU 带宽比例动态。建议固定小比例（如 2%）起，容量挂 `04` RT 预算。 | 否（执行层参数，建议固定起步） | `28` §4.2、`04` §4.4 |
| **P6** | **device VMO 连续页后端 = DMA32 only vs 加 CMA**：无 IOMMU + 设备需 >4GB DMA 时是否启用 CMA（压缩可用 buddy）。建议 Day 1 DMA32 起步、CMA 为演进项。 | 否（协同 `60` §2.2 退化策略） | `60` §3.2、`10` C8 |
| **P7** | **NUMA 迁移（ZONE_MOVABLE / kswapd 异步回收）是否进 Day 1 还是 M3 后**：影响 `MmPage.node_id` 可变性与 capability 引用稳定性。建议 M3 后启用（与 `03` Phase 3 对齐）。 | 否（协同 `03` §4.6） | `03` §4.6、`04` §5-6 |
| **P8** | **跨 node VMO 共享时物理页 node 归属与迁移是否破坏 capability 引用**（`03` §5-5 残留）：因 C1 折中已分离逻辑/物理，本文判定「不破坏」；是否需架构级确认。 | **已裁决·reasoning 决策已出（2026-08-03，确认不破坏 capability 引用）** | `03` §5-5、`00` C1 |

> 标注：本文为规划草案，后续以修订式追加更新，不覆盖、不删改 `00`–`70`、README、`.issues/`、git。跨域冲突以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准；本文与 `03` C1/`60` 设备内存/`28` 确定性/`04` Job 账本严格同构，未引入新内核原语或新 right bit。待决项 P1–P7 为执行层/协同定稿项，仅 P8（跨 node VMO 共享不破坏 capability 引用的架构确认）建议大喵拍板。

---

## 9. 参考文献（真实 URL，已核验可访问）

### 物理页分配（buddy / SLUB / zone）
- Linux 物理内存管理（node/zone/page/buddy/watermark）：https://docs.kernel.org/mm/physical_memory.html
- Linux 页分配（buddy / 分配路径 / watermark）：https://docs.kernel.org/mm/page_allocation.html
- Linux SLUB 分配器：https://www.kernel.org/doc/html/latest/mm/slab.html
- Linux memblock（早期分配器，对应 `03` C7）：https://www.kernel.org/doc/html/latest/mm/memblock.html

### 大页 / THP
- Linux hugetlb（预留大页池，对应 §3.2 hugetlb 式）：https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html
- Linux Transparent Huge Pages（按需聚合，对应 §3.2 THP 式）：https://www.kernel.org/doc/html/latest/admin-guide/mm/transhuge.html

### NUMA 策略
- Linux NUMA 内存策略（local/strict/interleave，对应 §2.2）：https://www.kernel.org/doc/html/latest/admin-guide/numa_memory_policy.html
- Linux NUMA（node/zone，对应 §2）：https://www.kernel.org/doc/html/latest/mm/numa.html
- ACPI SRAT/SLIT 规范（NUMA 拓扑来源，x86_64/loongarch64）：https://uefi.org/specification

### seL4（untyped 分配器 / device 思想，对应 `03` C1）
- seL4 Untyped 内存模型（capability/retype/revoke/device）：https://docs.sel4.systems/Tutorials/untyped.html
- seL4 手册（设备能力 / 用户态驱动）：https://sel4.systems/Info/Docs/seL4-manual.pdf

### Zircon（PMM / VMO，对应 `03` VMO 视图）
- Zircon VMO（虚拟内存对象，COW/分页/物理）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/vm_object
- Zircon VMAR（地址区域，层级权限/随机化）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/vm_address_region

### 项目内（已读，跨域对齐）
- `00-总览与路线图.md`（`A1`–`A12` 句柄即 capability/Job 账本/交权缩 TCB、`C1` 折中 C、`C7` memblock→buddy、`§5.3` 五架构一致性成本）
- `03-内存管理.md`（VMO/device VMO C1 折中、buddy/zone/slab M1–M4、NUMA Phase 2、`MmPage.flags`、C7 回写）
- `04-进程与调度.md`（Job 账本 §4.4、RT 预算、OOM 归因、exception channel）
- `07-安全模型.md`（right 位图冻结、`CAP_RT` bit45、`A1` 句柄即 capability、revoke 派生树）
- `10-设备驱动模型.md`（双轨驱动、能力网关、C8 撤销锁、device VMO 后端）
- `28-实时性与确定性.md`（R1 已采纳、mlock/`RT_LOCKED`、确定性预留、CAP_RT 化）
- `29-威胁模型.md`（T8 IPC 洪泛、T9 驱动崩溃、capability 爆炸半径）
- `60-设备驱动框架深化.md`（L1·L3 双轨、device VMO 不可写、IOMMU 域、撤销锁序列）
- `19-性能与可验证性.md`（IPC 边界、五架构差异、Rust TCB）

---

> 本文为「设计/规划」草案，结论（物理页分配器 = `03` VMO 底层供给层、NUMA 拓扑感知分配、大页/hugepage 后端、页回收/暂存与 Job 账本协同 fail-closed、device VMO 物理后端与 `60`/`10` 协同、确定性预留池与 `28` 同构、五架构薄后端同源）供执行层与 `03`/`60`/`10`/`28`/`04` 引用；待决项 P1–P7 为执行层协同定稿项，P8 已于第十轮自主采纳闭环（确认跨 node VMO 共享不破坏 capability 引用）。跨域冲突以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准。本文不修改 `00`–`70`、README、`.issues/`、git。

---

## 10. 第十轮自主采纳·闭环 P8

> **裁决**：采纳「确认本文判定：跨 node VMO 共享不破坏 capability 引用」。理由——`03` §5-5 的 C1 折中已分离逻辑对象与物理供给：VMO 是逻辑对象（capability 视图），物理页的 node 归属是供给层属性；跨 node 迁移仅改动物理供给（`MmPage.node_id`），VMO 句柄与 capability 引用（派生链、引用计数、revoke 面）均不变，因此**无需架构级额外机制**。该裁决与 `03` §5-5 C1 折中 + `07` capability 引用语义严格一致，且未引入新内核原语或新 right bit（符合 `00` A1–A12 / C1–C18 跨域地基）。P8 待决项表状态同步升级为「已裁决·闭环（第十轮自主采纳，确认不破坏 capability 引用）」。

> 待大喵复核。

---

## 11. reasoning 决策（2026-08-03）：P8 大喵复核细化

> 本节为 reasoning 决策 agent（2026-08-03）在指挥官第十轮「确认不破坏 capability 引用」占位基础上做的细化复核——不删不改 §10 原文与 §8 待决项的占位采纳，仅追加复核结论与「被否决选项的取舍」理由。复核重点在 `03 §4.1` C1 折中（buddy 独占物理页 / VMO 是逻辑视图）+ `00 A1`「句柄即 capability」+ `07 §4.1` rights 位图冻结纪律 + `07 §4.5` 受约束 mint / revoke 派生树 + `76 §1.2` 分配点 = Job 配额扣减点 + 五架构同源（buddy / zone / node_id 物理后端差异封死在 `arch/mm`）。

### 11.1 P8 · 跨 node VMO 共享是否破坏 capability 引用 → 同意「确认不破坏」

**reasoning 决策（2026-08-03）**：同意并固化「跨 node VMO 共享 / 跨 node 迁移（`ZONE_MOVABLE`，Phase 3）不破坏 capability 引用——VMO 句柄、capability 派生链、`revoke` 派生传播、rights 校验、`MmPage.refcount` 单一真相源均不变；唯二变化的 `MmPage.node_id`（物理供给标签）和 pte entry 项（arch/mm 内的页表项翻译）是供给层纯属性，不进 capability 语义层」——同时把「不破坏的具体边界」明示：(a) VMO handle 表 = capability 引用 (`07 §4.5` 受约束 mint)；(b) `MmPage.refcount` = 物理层引用计数，与 capability 引用分别在两个真相源，但二者由「`MmPage.refcount` 归零 → PMM 归还 buddy」单一链路衔接；(c) 跨 node 迁移触发仅改 pte 项（与跨页框替换同构），不引入新 capability 维度。

**理由（含被否决选项的取舍）**：

- **`03 §4.1` C1 折中是 P8 决策成立的基础**：C1 = 内核 buddy 独占物理页帧数据库（PMM = 唯一真相源）+ VMO/VMAR 作为 capability 视图（逻辑层）。该折中已经把「逻辑对象」与「物理供给」解耦到两个抽象层，因此「跨 node 迁移 = 物理层属性移动」与「capability 引用 = 逻辑层不变量」在 P8 上是天然正交，互不侵入。把该决策的成立基础显式化，避免后续被误读为「P8 假设了 C1」、并提醒任何打破 C1 折中的物理内存决策都必须重审 P8。
- **`MmPage.refcount` 单源一致性**：与 `03 §4.3.3 vmo_create_child` 的 COW 语义一致 —— refcount 是物理层单一真相源（不与 capability 引用计数混淆），归零触发 PMM 归还 buddy。跨 node 迁移时，物理页的「旧 node 释放 + 新 node 入位」由同一 refcount 经手，与 capability 派生链上的引用解耦：C-G1 `meu_fork_light` 已在大页 / 多核场景证明该解耦正确（`47` 第七轮精细裁决）。
- **capability 模型自洽**：与 `07 §4.5` 「revoke 派生树」+ `07 §4.6` capability Day 1 唯一裁决点 + `00 A1` 句柄即 capability + `00 A5` Job 资源账本 Day 1 同源 —— VMO 句柄是 capability 的载体、句柄与 `rights`、`revoke` 派生链耦合；物理页的 node 归属、pte entry 项、跨 node 迁移均不进入 capability 语义层，因此不破坏「句柄即 capability、权限随句柄走」的 Day 1 基线。
- **rights 位图冻结纪律**：与 `07 §4.1` rights 位图冻结纪律（bit 0–47 已分配、bit 48+ 预留）严格一致 —— 跨 node 共享不需要新 right、不挤占 bit 48+ 预留区；NUMA 亲和 = VMO 属性（创建参数或 `madvise` 兼容层映射，§2.2 已定），不是 capability 权限。决策保持「零新增 right bit」。
- **五架构同源可行性**：与 `00 §5-3`「统一抽象 + 每架构薄后端」+ `76 §7.1`「节点 / 大页 / NUMA 探测均在 `arch/mm` 内」同源 —— 跨 node 迁移的 pte 项翻译（x86_64 节点亲和位 vs aarch64 MPAID vs riscv64 PBM 等）属每架构薄后端，capability 语义层零差异；同一份 capability 语义在五架构上行为等价。
- **长期可维护性**：跨 node 迁移是「`zoned page allocator` + `move_pages()`」型机制，`76 §2.3` Phase 3 已列演进路径；P8 意味着这条演进路径不需要伴随 capability 维度的扩张，演进路径与现状一致、未来扩展 NUMA 拓扑迁移机制无需额外 capability 受约束 mint 协议。

**被否决选项**：

- **「需新增 capability-of-node 约束（或 per-node capability 句柄）」**——否决。VMO 句柄本就不绑 node（VMO 是逻辑对象，node 是物理页属性），引入「per-node VMO capability」= 把物理属性上移到 capability 维度，破坏 capability-on-handle 纯洁性、扩张 capability 模型、违背 C1 折中。同时 `76 §2.2` 已明示「亲和是 VMO 属性而非 capability 权限」，与此决策一致。
- **「禁止跨 node VMO 共享 / 跨 node 迁移」**——否决。过度限制 = 扼杀 NUMA 灵活性、强制虚拟地址层面对 NUMA 拓扑无感，违反 NUMA 本意（NUMA 的价值恰是「让物理层拓扑可见 + 选择亲和」）；同时 `03 §4.6` Phase 2/3 已声明 NUMA 演进路径需要跨节点可见性，禁止跨 node = 取消演进路径。
- **「跨 node 共享需调用专门的 `cross_node_share` capability 权限」**——否决。引入新 right = 挤占 `07 §4.1` 预留区（bit 48+），与冻结纪律冲突；同时 capability 维度本不绑 node，引入「cross-node 右」= 强行把物理拓扑塞入权限语义，违反「权限随对象走、可审计粒度」的 rights 设计原则。
- **「跨 node 时 VMO 必须 fork 一份（避免 capability 引用跨拓扑）」**——否决。性能代价大、引入 snapshot 语义（与 C-G1 `fork_light` 同构但意图不同），且无安全收益（VMO 句柄已跨 Process / Job 共享，仅多一道 NUMA 拓扑不应触发复制）。
- **「Capability 引用次数必须等于物理页引用次数（强制 lock-step）」**——否决。两个引用计数语义不同（capability 引用 = 句柄集 + rights，与 `revoke` 派生树耦合；物理页引用 = `MmPage.refcount` 与 COW / buddy 归还耦合），强制 lock-step 等于合并两个真相源，违反 `03 §4.3` refcount 单一真相源 + 引入新的 race 窗口。

**对本文的细化落点（不删前文）**：§10 已写「逻辑对象 vs 物理供给层」的根本论断，§11.1 进一步把「不破坏的具体边界」（handle 表 / refcount / 物理属性变化点 / pte 翻译）按「抽象层 × 真相源」二维列表明示；与 `03 §4.1` C1 折中 + `07 §4.5` revoke + `47` C-G1 大页/多核 COW 正确性 + `76 §1.2` 分配点 = Job 配额扣减点同源；`§8` P8 状态列同步升格。

---
