# 69 - 虚拟化与 guest 支持

> 独立调研一面（第七轮拓展·半新维度）· 调研 agent：lite / hy3
> 主题：虚拟化 guest 支持的深化设计——用户态 VMM + 薄内核 guest 设施、guest 内存（EPT/Stage-2）与 VMO/IOMMU 协同、guest 中断对象投影、virtio 设备模型、与 capability/Job/namespace/`.mimg` 信任链的协同，以及五架构优先级。本文是 `26-虚拟化与隔离.md` 在「guest 支持」这一面的单点深化（L2 细部），而非重复其 L1 总览。
> 性质：**纯新增独立调研文档**——解构型思考「guest 应作为 capability 经受限委托后跑不可信/异构负载的一种受限进程」，多系统参照（Fuchsia restricted VM / crosvm / pKVM / Hypervisor.framework），给出 MeuOS 推荐。**不改动 `00`–`68` 任何既有文件**（修订式追加纪律：仅在本文件内追加，结论如需回写由后续轮次执行）。
>
> 方法论铁律贯穿：解构型思维（先推演「应该是什么样」）、多系统参照、非缝合怪（思想取长补短、实现自主研究）、修订式追加不删。
> 关联文档：`07-安全模型.md`（capability / rights 位图 46·47·48+）、`04-进程与调度.md`（Job 账本/资源账本）、`26-虚拟化与隔离.md`（VMM 总览/五架构表/V1 已采纳）、`03-内存管理.md`（VMO/device VMO/C1 折中）、`10-设备驱动模型.md`（双轨/C8 撤销锁）、`60-设备驱动框架深化.md`（L1·L3/IOMMU domain）、`59-中断与异常子系统深化.md`（interrupt 对象/开放议题 5）、`61-内核自举与早期启动深化.md`（`.mimg`/四跳信任链/度量）、`25-网络栈与协议.md`（virtio-net/用户态网络栈）、`06-文件系统.md`（每进程 namespace/C3/`.msys`）、`00-总览与路线图.md`（A5·A6/C-G3）、`45-第五轮收敛摘要.md`（bit 46·47 冻结）。

---

## 0. 一句话核心结论

**MeuOS 的 guest 不是「第二种隔离原语」，而是 capability 经 `GUEST_CREATE`(bit46)/`VM_MANAGE`(bit47) 受限委托后、跑在用户态 VMM 进程（自身为一个受 Job 账本约束的容器）内的「不可信/异构负载」——内核只提供 `guest`/`vcpu` 一等对象与 EPT/Stage-2 薄后端，guest 内存由 VMO 承载、设备由 device VMO+IRQ cap 经 virtue 暴露、中断由 host `interrupt` 对象投影为 vCPU 注入，guest 映像度量并入 `.mimg`/`.msys` 信任链；五架构中仅 x86_64(VT-x/EPT) 与 aarch64(VHE/Stage-2) 先在 Day1 提供原生 VM，riscv64(H-ext) 可原型期验证，loongarch64 后置，i386 因无硬件虚拟化扩展不支持。**

---

## 1. 概述与目标

### 1.1 背景：guest 在 MeuOS 世界观里的「第一性」是什么

在 `07` 的 capability 唯一模型下，隔离的第一性不是「容器」也不是「VM」，而是 **capability 边界本身**——`Job` 资源账本（`04`）+ `rights` 权限位图（`07` §4.1）+ 每进程统一命名空间（`06`，C3 已采纳每进程独立）三件套天然构成「隔离单元」。`26 §1` 已论断：传统 VM 只是 capability 经 **device VMO（`03`）** 与 **CPU 时间（`04`）** 授予后、用来跑**不可信/异构负载**（旧 OS、隔离测试、第三方内核）的一种**受限 guest**；兼容层（`08`）已定「薄翻译层非 VM」，故 VM **不**用于跑 Linux 二进制。

本文在 `26` 的 L1 总览之上，单点深化「guest 支持」这一面，回答六个 `26` 留白或更细的问题：用户态 VMM 进程如何落进 Job 账本、guest 内存的二级页表与 VMO/IOMMU 如何协同、guest 内中断对象如何投影、virtio 设备如何以 capability 暴露、以及 guest 映像如何并入 `.mimg` 信任链。

### 1.2 本文范围与边界

- **范围**：guest 支持的设计/规划，含门禁、内存、中断、设备、信任链、架构优先级。
- **不在范围**：薄内核 `guest`/`vcpu` 对象的具体实现（属执行层）；`26 §5` 已定的 VMM 总览架构不复述。
- **纪律**：只写设计/规划文档，不写实现代码；不引入新内核原语，除非 capability 模型无法承载（本文所有方案均建立在既有 `07` rights 与 `04` Job 之上）。

### 1.3 与 `26` 的分工

| 维度 | `26-虚拟化与隔离.md` | 本文 `69` |
|------|----------------------|-----------|
| 层级 | L1 总览（方案选型/五架构表/TCB 归属） | L2 细部（guest 支持的门禁/内存/中断/设备/度量） |
| VMM 门禁 | 提出 `GUEST_CREATE`/`VM_MANAGE` 建议 bit（§5/§7 V2） | 引用 `07 §4.1` 已冻结 bit46/47，给出「谁持有、如何委托」 |
| 中断 | 开放议题指向 `59` | 深化 guest 内投影方案 + bit48+ 协调 |
| 度量 | 未展开 | 并入 `.mimg`/`.msys` 四跳信任链（`61`） |

---

## 2. VMM 用户态架构（与 `00` A6 一致）

### 2.1 方案：VMM 进程 = Job 账本中的一个 guest 容器

遵循 `00` A6「重服务出核、收缩 TCB」与 `26 §5` 的 Fuchsia 式路线，**VMM 是用户态 Rust 进程**（`00` A6 的典型受益者），而非内核子系统。该 VMM 进程自身在 `04` 的模型中表现为一个 **Job**——它携带 `GUEST_CREATE`(bit46) 与 `VM_MANAGE`(bit47) 两个专属 right（`07 §4.1`，默认 deny、仅 VMM 服务持有），并作为 guest 的**账本宿主（Job ledger owner）**：

- **创建**：VMM 以 `GUEST_CREATE` 调内核薄设施，产出 `guest` 一等对象 + 若干 `vcpu` 对象（`26 §5`）。
- **生命周期**：每个 guest 作为一个 **Job** 接入 `04` 资源账本——CPU 时间配额、内存配额、句柄配额、IO 配额、pid 配额（`04 §4.4`）全部以该 Job 为单元计量与回收；guest 销毁 = Job 账本该子树被回收，等价于 capability 派生树回收（`07 §4.5` 约束式 mint 的回收语义）。
- **spawn 语义**：guest 内再 spawn 的 vCPU/辅助线程，是 VMM Job 下的子账本项，不逃逸 VMM 的配额（`04 §4.2` spawn 语义）。

### 2.2 能力门禁：bit46 / bit47 的委托链

`07 §4.1`（由 `45 §3` 冻结回写）已定：

| bit | right | 语义 | 默认 | 持有者 |
|----|-------|------|------|--------|
| 46 | `GUEST_CREATE` | 创建 guest/vcpu 对象 | deny | 仅 VMM 服务 |
| 47 | `VM_MANAGE` | 管理 VM / 设备直通委托 | deny | 仅 VMM 服务 |

委托链遵循 `07 §4.5` 约束式 mint（无 root、不可放大派生）：**VMM 服务**的初始令牌由早期启动 Stage 4 授予（`61` 与 `09` Stage 4 同源），其后 VMM 只能以**等权或收窄**的派生把 `guest`/`vcpu` 句柄与 `VM_MANAGE` 下的设备委托权交给具体 guest 管理子进程——绝不可把 `GUEST_CREATE` 自身下放（创建权保留在 VMM 服务根），与 `07 §4.1`「专属 bit、deny、仅 VMM 服务」一致。

### 2.3 权衡

- **利**：TCB 最小化——设备模拟（virtio-blk/net、控制台、看门狗）全在用户态，内核不背 KVM 式 ioctl 大 ABI（`26 §5` 否决项）；guest 与 host 的边界 = capability 边界，无「root 逃生舱」；与 `00` A6 收缩 TCB 同构。
- **弊**：每次 vCPU 退出（VM-exit/同步异常）需 VMM 用户态处理再重入，较 KVM 内核态处理多一次上下文/拷贝开销；但 Fuchsia/crosvm 实践已证明该开销对 MeuOS 目标负载（隔离测试、第三方内核、异构 OS）可接受。
- **拒**：Linux KVM 式「单体内核虚拟化子系统 + `/dev/kvm` 大 ioctl 面」——ioctl 面是需长期稳定的 ABI（与 `23` 冲突），且把设备模拟拉进 TCB。`26 §7 V1` 第四轮已采纳「不提供 KVM 兼容接口」，本文沿用，不再列为待决。

### 2.4 推荐立场

**VMM 必须用户态**（一致性：`00` A6 + `26 §5` + `26 §7 V1` 已采纳）；guest 作为一个 **Job** 接入 `04` 账本，其创建/管理权经 `GUEST_CREATE`(46)/`VM_MANAGE`(47) 受限委托，默认 deny、不可下放创建权。本立场零新增 right bit（46/47 已冻结）、零新增内核原语。

---

## 3. guest 内存：EPT / Stage-2 与 VMO 协同

### 3.1 方案：GPA 由 VMO 承载，二级页表是内核薄后端的「投影」

guest 物理地址空间（GPA）不以独立页表孤岛存在，而是**由 `03` 的 VMO 承载**：VMM 为 guest 创建一个 VMO（或一组 VMO 拼成 guest 物理空间），内核 guest 设施据该 VMO 维护 **EPT（x86_64）/ Stage-2 页表（aarch64）** 作为二级地址翻译的薄后端（`26 §5.3`）。即：

- GPA → HPA 的映射 = VMO 页框的二级投影；guest 看到的「物理内存」即 VMO 内容。
- guest 内存配额 = 该 VMO 的容量，受 `04` Job 账本内存配额约束（`04 §4.4`）；超出即 VMO 扩容失败，guest 内 OOM，不波及 host。
- 共享内存（virtio 队列/缓冲区） = VMO 的派生（`03` VMO 派生树），host 与 guest 经同一 VMO 的不同句柄视图通信，复用 `03` 的 revoke=回收 语义。

### 3.2 与 IOMMU domain（`10`/`60`）协同——设备直通的内存侧

当 guest 做 **设备直通（passthrough）** 时，设备 DMA 必须被限制在 guest 的 GPA 范围内，否则越界 DMA 击穿隔离。这要求 guest 的内存视图与 **IOMMU domain** 对齐：

- `10`（双轨 + C8 撤销锁）与 `60`（L3 用户态默认、IOMMU domain 隔离）定义 per-driver IOMMU domain；guest 直通设备时，其 **Stage-2 映射与 IOMMU domain 页表必须同源**——同一份 VMO 页框既进 EPT/Stage-2 也进 IOMMU 二级映射（`60 §3` 第②层 domain I/O cap 委托的硬件落地）。
- **强制 L3 per-driver domain**：直通设备不得共享 host IOMMU 域，避免一个 guest 的 DMA 触达另一 guest 或 host（`60` L3 默认 + `29` 威胁模型的设备隔离强度）。
- 撤销（设备拔出/guest 销毁）= `10` C8「撤销锁序列」同时回收 device VMO 与对应 IOMMU domain 映射，与 `59 §4.4` 中断对象生命周期同构。

### 3.3 与 `03` VMO / device VMO 协同

- **guest RAM** = 普通 VMO（可写，由 buddy 拥有的物理页，`03 §2.1`）。
- **MMIO 透传** = `03` 的 **device VMO**（不可写、seL4 device-untyped 思想，C1 折中 C 已采纳）——guest 内设备寄存器的映射即 device VMO 在 Stage-2 的投影，host 侧由 VMM 持 device VMO + `Cap(IRQ n)`（`10`/`59`）模拟或直通。
- **revoke** = capability 派生树回收：`03` 的 VMO 派生树回收即 guest 内存回收，无「页框泄漏」逃逸。

### 3.4 权衡

- **利**：内存隔离 = capability 派生树隔离，无新增内存抽象；IOMMU domain 与 Stage-2 同源杜绝 DMA 越界；配额天然接入 `04` Job。
- **弊**：VM-exit 时的 GPA→HPA 缺页需 VMM 介入补全 VMO 映射，较裸金属多一次边界穿越；但这是用户态 VMM 路线的固有成本，已被 `26 §5` 接受。
- **注意**：i386 无 EPT/Stage-2 硬件，guest 内存二级映射无硬件基础（`26 §5.3` i386 不支持原生 VM），其 guest 内存只能经纯软件阴影页表模拟——本文按 `02 §2.3` 约束 C2（i386 纯 C、无 Rust 裸机 target）**不在 Day1 支持 i386 原生 VM**。

### 3.5 推荐立场

**guest 内存一律以 VMO 为唯一truth source，EPT/Stage-2 与 IOMMU domain 均是其二级投影；直通设备强制 per-driver IOMMU domain（L3）。** 零新增内存原语（复用 `03` VMO/device VMO + `10`/`60` IOMMU domain）。

---

## 4. guest 中断对象投影（与 `59` 开放议题 5 协同）

### 4.1 问题：`59 §9.3` 开放议题 5 的精确语义

`59 §9.3` 开放议题 5 明确：**「guest 经 VMM 拿 `Cap(IRQ n)` + device VMO 直通，中断对象模型在 guest 内如何投影（vCPU 注入），待 `26` 细化」**。本文承接该细化：当中断到达 host 的 `interrupt` 对象（`59` 模型，`Cap(IRQ n)` 带 `IRQ_WAIT`(bit52)/`IRQ_ACK`(bit53)/`IRQ_MASK`(bit54)，均已在 `07 §4.1` 48+ 预留区冻结），而该 IRQ 属于某个 guest 直通设备时，host 内核**不**在 host 侧直接消费该中断，而是**投影为 guest 内的虚拟中断（VIRQ）并经 vCPU 注入**。

### 4.2 方案：VMM 持 host `interrupt` 对象，经 vCPU 注入投影

投影链（不新增内核对象类型，复用 `59` 的 `interrupt` 对象 + `26` 的 `vcpu`）：

1. VMM 经 `VM_MANAGE`(bit47) 委托获得某直通孔设备的 `Cap(IRQ n)`（host 侧 `interrupt` 对象，`59` 模型）。
2. 硬件 IRQ 触发 → host 内核中断管理器路由到该 `interrupt` 对象（`59 §4.x` 的 Virq 绑定）→ VMM 用户态 `IRQ_WAIT`(bit52) 消费。
3. VMM 据设备所属 guest，将中断**投影**为该 guest 的一个 VIRQ，调用薄内核 `vcpu_inject(vcpu, virq)` 注入（`26 §5` 的 `vcpu` 对象方法）。
4. guest 内以自身中断控制器（如 GIC/APIC 模拟或直通的 vGIC/vAPIC）处理；guest 的 EOI 经 VMM 转发为 host 侧 `IRQ_ACK`(bit53)。

要点：**guest 内不持有 host `Cap(IRQ n)` 本身**（那会突破 capability 边界），guest 只看到「被 VMM 投影的 VIRQ」；host `interrupt` 对象的持有者始终是 VMM，投影是 VMM 的软件行为，不是新的内核授权原语。

### 4.3 bit48+ 预留区协调（核心待决）

`59` 已在 `07 §4.1` 48+ 预留区冻结 `IRQ_WAIT`(52)/`IRQ_ACK`(53)/`IRQ_MASK`(54)——但这些是 **host 侧 per-IRQ right**，描述的是「host 驱动/VMM 如何操作 host `interrupt` 对象」，**不描述「guest 内投影出的 VIRQ 对象」是否也需一个独立 right**。

开放问题：**guest 内的投影中断对象（VIRQ）是否需要在 `07` rights 位图中占据一个新的 bit（候选 bit56+，48+ 预留区尚余 56+ 未分配）**，例如 `GUEST_INTR_INJECT`（VMM 经 `VM_MANAGE` 委托后、向指定 guest 注入 VIRQ 的专属权）？两种立场：

- **立场 A（最小新增）**：VIRQ 注入权**复用 `VM_MANAGE`(bit47)**——「能管 VM 自然能向其注入中断」，不新增 bit；投影是 VMM 内部行为，内核只需一个 `vcpu_inject` 系统动作（受 `VM_MANAGE` 门禁）。
- **立场 B（显式分离）**：新增 `GUEST_INTR_INJECT`(bit56+) 将「管理 VM」与「向 guest 注入中断」解耦，使未来可把注入权委派给独立的中断代理进程而非整个 VMM，细化权限粒度（类比 `IRQ_WAIT`/`IRQ_ACK`/`IRQ_MASK` 因语义独立而拆分，`59` 已采纳该拆分哲学）。

### 4.4 权衡

- 立场 A 与 `07 §4.1`「不复用 MANAGE/GRANT、默认 deny」纪律存在张力——`VM_MANAGE` 本是「管理 VM/设备直通委托」的复合权，把中断注入也塞进去会使其语义过载；但 Day1 仅 VMM 持 `VM_MANAGE`，无即时越权风险。
- 立场 B 更符合 `59` 的「语义独立即拆分」哲学，但引入一个新 bit，需 `07` owner 协同定稿（遵循 `45 §3` 统一分配纪律），属 bit48+ 预留区协调项。

### 4.5 推荐立场 + 待大喵拍板

**Day1 推荐立场 A（复用 `VM_MANAGE`(47) 注入，不新增 bit）**——理由：Day1 仅 VMM 服务持 `VM_MANAGE`，无细分必要；投影是 VMM 软件行为，内核 `vcpu_inject` 动作受 `VM_MANAGE` 门禁即足够。但**是否升级到立场 B（新增 bit56+ `GUEST_INTR_INJECT`）属架构级待决，需大喵拍板**（见 §8 V1）。嵌套虚拟化（`26 §7 V6`）场景下立场 A 会显出粒度不足，可届时再开 B。

---

## 5. virtio 设备模型

### 5.1 方案：virtio-blk/net 作为用户态驱动

guest 的设备面以 **virtio** 半虚拟化为主（`26 §5` 路线），具体驱动落在用户态：

- **virtio-blk**：用户态块设备驱动（`60` L3 默认 + `33-块设备与存储抽象`），后端为 host 侧 VMO（guest 磁盘镜像或 `.msys` 卷的一部分，`06`）。
- **virtio-net**：用户态网络驱动（`25`，netstack 全用户态 C15 已采纳），后端为 host 网络栈（`25` 的 NET_* right，bit28–31 / `NET_MANAGE`=bit39）；guest 网络流量经 VMM 用户态桥接到 host netstack。
- **控制台/uart**：早期串口经 `60` E1 第②层 `CONSOLE_*`/`UART_*`(bit49) 暴露。

### 5.2 设备以 capability 暴露给 guest

设备不是「全局资源」，而是经 capability 受限委托给 guest（`07` 模型 + `26 §5`）：

- **半虚拟化（virtio）**：VMM 持后端 device VMO + host `interrupt` 对象，在 guest 内以 virtio 队列（共享 VMO，`03` 派生）暴露；guest 不持 host device VMO 本身，只看到 virtio 抽象。
- **直通（passthrough）**：VMM 经 `VM_MANAGE`(47) 把 device VMO（`03`，不可写）+ `Cap(IRQ n)`（`59`）委托给 guest，guest 内以原生设备驱动访问；同时该 device VMO 进 guest 的 per-driver IOMMU domain（`60` L3，§3.2 协同）。

### 5.3 与 namespace（`06` C3）协同

guest 内看到的 `/dev`/`/proc`/`/net` 是 **guest 自身每进程 namespace**（`06` C3 已采纳每进程独立）的子集——VMM 在 spawn guest 时为其构造一个受限 namespace，仅挂载经 capability 委托的设备节点（`06 §4.3` mount-capability 委托）。guest **不**继承 host namespace，杜绝 namespace 逃逸（与 `07` capability 边界一致）。

### 5.4 推荐立场

**guest 设备以 virtio 半虚拟化为主、直通为辅；两者均经 capability（device VMO + IRQ cap）受限委托，直通强制 per-driver IOMMU domain（L3）；guest namespace 由 VMM 构造且受限。** 零新增设备原语（复用 `60`/`25`/`06`/`03`）。

---

## 6. 与 capability / Job / namespace / `.mimg` 信任链的协同

### 6.1 与 capability（`07` §4.1）协同

- **创建/管理门禁**：`GUEST_CREATE`(46) / `VM_MANAGE`(47) 已冻结，默认 deny、仅 VMM 服务（`07 §4.1` + `45 §3`）。
- **约束式 mint**：guest/vcpu 句柄与设备委托均经 `07 §4.5` 约束式 mint 派生，无 root、不可放大。
- **bit48+ 预留区**：host `interrupt` 对象的 `IRQ_WAIT`/`IRQ_ACK`/`IRQ_MASK`(52–54) 已冻结（`59`）；guest 投影中断是否新增 bit56+ 见 §4.3 / §8 V1。bit48 维持 `SCENE_SESSION` 候选描述不占位（`07 §4.1` C-G3）。

### 6.2 与 Job 账本（`04`）协同

- guest = 一个 **Job**（`04 §4.1`），CPU/内存/句柄/IO/pid 配额以 Job 为单元（`04 §4.4`）。
- VMM 进程自身也是 Job，guest Job 是其子账本；guest 销毁 = Job 子树回收 = capability 派生树回收（与 `07 §4.5` 同构）。

### 6.3 与 namespace（`06` C3）协同

- guest 内 namespace 由 VMM 构造且受限（`06` C3 每进程独立），仅挂载经 capability 委托的设备（`06 §4.3` mount-capability）。
- `.msys` 根以原子 repoint 挂载（`06 §4.3`），guest 根 fs 可选以 `.msys` 卷的子集或独立 VMO 镜像承载。

### 6.4 与 `.mimg` 信任链及 guest 映像度量（`61`）协同

`61` 的四跳信任链（§1.2）：PCR[0] 固件→loader、PCR[1] `.mimg`、PCR[2] monitor 自度量、PCR[3] `.msys` rootfs。guest 映像度量应**并入而非另起炉灶**：

- **guest 内核/固件映像**作为 VMM 加载的载荷，其度量应 extend 进同一信任链（建议复用 PCR[1] 之后的 guest 专属 PCR 槽，或作为 `.mimg` Item 的 guest measurement 子项，`61 §3` 的 `.mimg` 单容器格式可扩展携带 guest 度量元数据）。
- **guest rootfs**（若来自 `.msys` 卷子集）复用 `.msys` 的 Merkle 度量（`61 §5.3` + `06 §4.3` extent 校验和）。
- **度量基双锚**（`32 §3.2`）与公钥根档 A/B/C（`32 §3.3`）对 guest 映像同样适用——guest 映像须由已锁定的公钥验签，杜绝「未经验证的 guest 内核」成为新的信任源（`61 §1.2` 原则 1）。
- VMM 在 `GUEST_CREATE` 前应先完成 guest 映像验签+度量，否则拒绝启动（fail-closed，与 `07` pledge/unveil 一致）。

---

## 7. 五架构优先级

### 7.1 架构支持差异（`26 §5.3` 表沿用 + guest 支持细化）

| 架构 | 硬件虚拟化扩展 | Day1 guest 支持形态 | 备注 |
|------|----------------|---------------------|------|
| **x86_64** | Intel VT-x（VMX）/ AMD AMD-V（SVM） | **原生 L2 VM（guest 设施 + vCPU + EPT/NPT）** | 五架构最成熟；EPT/NPT 二级映射即 §3 的 VMO 投影后端 |
| **aarch64** | VHE（EL2 作 host）+ NV（嵌套，后续） | **原生 L2 VM**；host 跑 EL2、guest EL1/EL0 | VHE 使 host 直接用 EL2，免去 EL1 stub；Stage-2 即 §3 后端 |
| **riscv64** | H-extension（hypervisor，2021）+ vs-mode | **原型期可支持**（依赖 QEMU/硬件 H-ext） | 优先级次于 x86/aarch64；先用 QEMU `virt` H-ext 验证 guest 设施抽象 |
| **loongarch64** | LoongArch 虚拟化扩展（LSVZ/LVZ） | **后置**：扩展存在但工具链/验证成熟度低 | 需新增 arch 后端，原型期可后置 |
| **i386** | **无**（VT-x 仅 64 位） | **不支持原生 VM** | `02 §2.3` 约束 C2：纯 C、无 Rust 裸机 target，VM 后端更无硬件基础 |

### 7.2 统一抽象原则

内核 guest 设施抽象为 `arch/vm/` 接口（`guest_create` / `vcpu_run` / `ept_map` / `stage2_map` 等），各架构提供薄后端；**无硬件虚拟化扩展的架构（i386）该后端直接返回 `ENOSYS`，不模拟**（`26 §5.3` 统一抽象原则）。Day1 只实现 x86_64 与 aarch64 两个后端；riscv64 后端在原型期补；loongarch64/i386 后端留 `ENOSYS` 桩。

---

## 8. MeuOS 推荐（收口）

### 8.1 立场汇总

1. **VMM 用户态**（一致于 `00` A6 + `26 §5` + `26 §7 V1` 已采纳）：guest 作为 **Job** 接入 `04` 账本；创建/管理经 `GUEST_CREATE`(46)/`VM_MANAGE`(47) 受限委托，默认 deny、创建权不下放。
2. **guest 内存以 VMO 为 truth source**：EPT/Stage-2 与 IOMMU domain 皆为其二级投影；直通强制 per-driver IOMMU domain（L3）。
3. **guest 中断投影**：host `interrupt` 对象（`59`）经 VMM 投影为 VIRQ 注入 vCPU；Day1 复用 `VM_MANAGE`(47) 注入，不新增 bit（立场 A）。
4. **virtio 为主、直通为辅**：设备经 capability（device VMO + IRQ cap）受限委托；guest namespace 由 VMM 构造且受限（`06` C3）。
5. **guest 映像度量并入 `.mimg`/`.msys` 信任链**（`61`）：fail-closed 验签+度量后才 `GUEST_CREATE`。
6. **五架构**：x86_64(VT-x/EPT) 与 aarch64(VHE/Stage-2) Day1 原生；riscv64(H-ext) 原型期；loongarch64 后置；i386 不支持。

整体零新增内核原语、零新增 right bit（除 §8.2 V1 候选 bit56+ 待决），全部建立在 `07`/`04`/`03`/`10`/`60`/`59`/`61`/`26`/`06`/`25` 既有地基之上。

### 8.2 待决项（标注是否需架构裁决 / 大喵拍板）

| 编号 | 待决项 | 是否需大喵拍板 | 关联 |
|------|--------|----------------|------|
| **V1** | **guest 内投影中断对象（VIRQ）是否新增 `GUEST_INTR_INJECT`(bit56+) 显式 right，还是 Day1 复用 `VM_MANAGE`(47) 注入（立场 A）？** 本文 Day1 推荐立场 A，但嵌套虚拟化（`26 §7 V6`）场景下粒度不足，届时可能需立场 B。 | **已裁决·闭环（第九轮自主采纳，立场 A·不新增 bit56+）** | `59 §9.3` 开放议题 5 / `07 §4.1` 48+ 预留区 / `45 §3` 分配纪律 |
| **V2** | **guest 映像度量如何并入 `.mimg` 信任链**：扩展 `.mimg` Item 携带 guest measurement 子项（`61 §3`），还是独立 `.mguest` 侧载？度量 extend 进哪个 PCR 槽？ | **已裁决·闭环（第九轮自主采纳，扩展 .mimg Item）** | `61 §1.2`/`§3`/`§5` + `32 §3.2`/`§3.3` + `06 §4.3` |
| **V3** | 设备直通时 **Stage-2 与 IOMMU domain 同源绑定**的强制语义是否写进 `60` L3 约束（即「直通=强制 per-driver IOMMU domain」是否列为不可放宽项）。 | 否（执行层/`60` 协同定稿） | `60 §3`/`§6` + `10` C8 + `29` 威胁模型 |
| **V4** | riscv64 H-ext 与原生 VM 的验证优先级是否先于 loongarch64（`26 §7 V4`）。 | 否（路线级） | `11` 自举验证 / `09` 多架构 |
| **V5** | 若 V1 采纳立场 B，bit56+ 的具体分配（建议 bit56 `GUEST_INTR_INJECT`），由 `07` owner 协同 `59` 定稿。 | 否（执行层/`07` 协同，依赖 V1） | `07 §4.1` 48+ 预留区 / `45 §3` |
| **V6** | guest 设施是否支持嵌套虚拟化（x86_64 VMX / aarch64 NV）——原型期一律不支持（`26 §7 V6`）。 | 否（演进级） | `05 §5` / `09` 多架构 |
| **V7** | guest 设备默认以 virtio 半虚拟化为主、直通为辅的取舍是否写入 `60` 设备框架（直通门槛/默认策略）。 | 否（执行层/`60`/`25` 协同） | `60` + `25` + `33` |

> **核心待决（须大喵拍板）**：**V1**（guest 投影中断是否新增 bit56+ right）与 **V2**（guest 映像度量并入 `.mimg` 信任链的范围）——其余 V3–V7 为执行层/路线级，可在后续轮次收敛，不阻塞 Day1 自举。

---

> **一句话核心结论（收口）**：MeuOS 的 guest 是 capability 经 `GUEST_CREATE`(46)/`VM_MANAGE`(47) 受限委托、跑在用户态 VMM（自身为 `04` Job）内的受限异构负载——内核只给 `guest`/`vcpu` 一等对象与 EPT/Stage-2 薄后端，guest 内存以 VMO 为 truth source 并与 IOMMU domain 同源、中断由 host `interrupt` 对象投影为 vCPU 注入、设备以 virtio/直通经 capability 暴露、映像度量并入 `.mimg`/`.msys` 信任链，五架构仅 x86_64/aarch64 Day1 原生支持；唯一需大喵拍板的是 V1（投影中断是否新增 bit56+ right）与 V2（guest 度量并入信任链的范围）。
> 关联索引：本文为第七轮拓展「独立调研一面」，纯新增 `69`；深化 `26`/`59`/`61`，与 `07`/`04`/`03`/`10`/`60`/`25`/`06`/`00`/`45` 零冲突。结论如需回写（如 bit56+ 分配、`00` 冲突登记表补 C-VM 项、`.mimg` 扩展 guest measurement Item），由后续修订轮次以「修订式追加」执行，本文件不改 `00`–`68` 任何内容。

---

## 9. 第九轮自主采纳·闭环 V1 / V2

> 以下为指挥官第九轮自主采纳裁决，以「修订式追加」回写，不删改 §1–§8 既有原文；仅 §8.2 表中 V1、V2 两行状态字段升级为「已裁决·闭环」。本文纯设计/规划，不写实现代码；不引入新内核原语，仅涉及 right 的新增按纪律「候选预留 bit56+ 不分配」（V1 立场 A 即不新增）。

### 9.1 V1 — guest 投影中断不新增 `GUEST_INTR_INJECT`(bit56+)，Day1 复用 `VM_MANAGE`(47)

**采纳结论**：Day1 复用 `VM_MANAGE`(bit47) 注入 VIRQ，**不新增 bit56+ `GUEST_INTR_INJECT`**。

**理由**：Day1 仅 VMM 服务持 `VM_MANAGE`，无细分必要；投影是 VMM 软件行为，内核 `vcpu_inject` 受 `VM_MANAGE` 门禁即足够。嵌套虚拟化（`26 §7 V6`）场景若显出粒度不足，届时再开立场 B（新增 bit56+ 候选，由 `07` owner 协同 `59` 定稿，列为新增冻结），不阻塞 Day1。

**衔接**：
- 与 `59 §9.3` 开放议题 5 一致——guest 中断对象投影由 VMM 经 host `interrupt` 对象完成，VIRQ 注入权归 `VM_MANAGE`，不另占 `07 §4.1` 48+ 预留区。
- 与 `07 §4.1` 48+ 预留区纪律一致：`GUEST_INTR_INJECT` 仅「候选预留 bit56+ 不分配」；立场 B 触发时，具体分配（建议 bit56）须由 `07` owner 协同 `59` 定稿，并遵循 `45 §3` 统一分配纪律。
- 与 `45 §3` 分配纪律一致：本轮不新增任何 right bit，故不触发 bit56+ 分配流程，`00` 冲突登记表亦不补 C-VM 项。
- V1 立场 A 闭环后，原 §8.2 V5（依赖 V1 立场 B 的 bit56+ 分配）随不触发而自然消解，留待立场 B 真正启用时再议。

### 9.2 V2 — guest 映像度量以扩展 `.mimg` Item 并入信任链

**采纳结论**：扩展 `.mimg` Item 携带 guest measurement 子项，**不引入独立 `.mguest` 侧载**；guest 度量 extend 进独立 PCR 槽。

**理由**：与 C61-2（度量元数据内嵌 item）同构——guest 度量作为 `.mimg` 单容器的 Item 子项内嵌，而非另起 `.mguest` 侧载文件；guest 度量 extend 进**独立 PCR 槽**（复用 `32 §3.2` 双锚 + `61 §3` 度量基），不挤占既有 PCR[0..4]；保持 `.mimg` 唯一启动载体（C61-1）。

**衔接**：
- 与 `61 §3` 一致：`.mimg` 单容器格式可扩展携带 guest measurement 子项，信任链仍以 PCR[1] `.mimg` 为锚，guest 度量续接独立 PCR 槽。
- 与 `32 §3.2` 度量基双锚 + `32 §3.3` 公钥根档 A/B/C 一致：guest 映像须由已锁定公钥验签，杜绝「未经验证的 guest 内核」成为新信任源（`61 §1.2` 原则 1）。
- 与 C61-1/C61-2 一致：保持 `.mimg` 唯一启动载体、度量元数据内嵌 item，不破坏四跳信任链既有结构，不新增信任链侧载原语。
- VMM 在 `GUEST_CREATE` 前须先完成 guest 映像验签 + 度量（fail-closed），与 `07` pledge/unveil 一致；度量结果落独立 PCR 槽，不干扰 host PCR[0..4]。

> **待大喵复核**：本两项（V1 立场 A·不新增 bit56+；V2 扩展 `.mimg` Item 并入信任链）为指挥官第九轮自主采纳，待大喵确认。
