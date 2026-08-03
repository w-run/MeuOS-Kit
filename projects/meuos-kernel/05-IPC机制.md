# 05 - IPC 机制（进程间通信）

> 子领域调研 / 规划设计文档，由 kernel-plan 团队的 IPC 调研员（k-ipc）产出。
> 适用范围：MeuOS 自研内核的「统一 IPC 原语」设计提案。
> 关联约束：混合内核、Rust+C 混合、capability-based 安全 Day 1、外部兼容 Linux ABI（兼容层）。

---

## 1. 概述与目标

### 1.1 问题陈述

进程间通信（IPC）是任何多进程 OS 的骨架。Linux 的 IPC 谱系在数十年演进中积累了十余种互不相通的机制：管道（pipe/fifo）、Unix 域套接字（AF_UNIX）、System V / POSIX 消息队列、System V / POSIX 共享内存、信号量、futex、eventfd、signalfd、timerfd、epoll，以及用户态的 D-Bus 和（Android 专属的）Binder。这些机制语义各异、权限模型不统一、事件通知被拆成 epoll+eventfd+signalfd+timerfd 四套拼装，且多数以「文件描述符 + 文件系统命名空间 + 凭据校验」来表达本应是「权限授予」的语义，导致概念爆炸与安全边界模糊。

MeuOS 的既定取向是：**不发明新概念，但组合出比 Linux 干净一个数量级的内核 IPC**。具体地：

- 同步端点取 **seL4 / Zircon**（同步、带类型、可组合）；
- 事件通知取 **kqueue**（单一统一接口，替代 epoll+eventfd+signalfd+timerfd）；
- 服务间通信取 **Binder / XPC**（对象引用 + 事务语义 + 内核级服务注册激活）；
- 所有 IPC 端点即 **capability**（与 MeuOS 的 capability-based 安全 Day 1 模型天然融合）。

### 1.2 设计目标

1. **统一原语**：用尽量少的原语覆盖 Linux 十余种 IPC 的全部常用语义。本提案收敛为四类——Channel（消息通道）、EventPort（统一事件）、共享内存对象（SHM）、服务命名表（Service Registry）。
2. **端点即 capability**：每个 IPC 端点、通道端、共享内存、事件端口都是 capability；持有 capability 才可使用，权限显式授予，杜绝「文件权限位 + 命名空间 + socket 凭据」的多方拼凑。
3. **同步 + 异步统一**：同步 RPC（call/reply）与异步事件通知共享同一套内核对象与事件模型，不引入割裂的 API。
4. **带类型、可组合**：消息载荷带类型标记，可携带 capability 句柄（对象引用），从而原生表达 Binder 式「对象引用传递」与事务语义，无需丑陋的 `SCM_RIGHTS` 辅助数据。
5. **内核级服务发现与按需激活**：借鉴 XPC/launchd，服务名注册在内核命名表，连接未运行的服务时由进程管理器（spawn 模型）按需拉起，而非经由用户态 broker 中转。
6. **Linux ABI 可映射**：上述原语必须能无损（或近似无损）地支撑 Linux 兼容层（mkit）对 pipe/AF_UNIX/eventfd/signalfd/timerfd/epoll/shm 的仿真。

### 1.3 非目标（明确边界，避免过度设计）

- 不重新设计网络协议栈 / 跨机 RPC（跨主机通信仍走 socket 兼容层，不在本原语内）。
- 不内置一套全新的 IDL/序列化格式；消息载荷的「类型」仅指「长度 + 携带 handle 数量 + 可选 type tag」，上层 AIDL/自定义 schema 由库负责。
- 不替代调度器、不替代地址空间管理；IPC 只做「受控的消息与能力传输」。

---

## 2. 设计约束与上下文

### 2.1 内核形态：混合内核

MeuOS 是混合内核（hybrid kernel）。这意味着 IPC 不必像纯微内核那样承担「几乎所有系统服务调用」的极端开销压力，但也保留了「服务进程隔离、能力委托」的微内核遗产。因此：

- 多数系统服务（驱动、文件系统等）可在内核态或受监督的用户态服务进程中运行；
- IPC 既要足够快（内核内直连拷贝，走 fastpath），又要能跨越用户态服务边界；
- 与纯微内核（如 seL4，所有驱动都在用户态、IPC 是系统调用热点）不同，MeuOS 的 IPC 不必为「驱动调用」承担全部系统调用负载，但在「服务进程间通信」场景仍须有 seL4 级别的 fastpath。

### 2.2 语言策略：Rust + C 混合

- IPC 内核对象（Channel、EventPort、SHM、Service Registry）的实现可用 Rust（类型安全、最小 TCB）承载核心状态机，C 承载与工具链/兼容层的衔接。
- capability 表（CSpace）的访问路径是安全敏感面，用 Rust 保证其不变量。

### 2.3 capability-based 安全 Day 1

MeuOS 将 capability 作为 Day 1 唯一权限模型（pledge/unveil 沙箱）。IPC 设计必须满足：

- 一切通信端点都是 capability，创建/导出/连接都需要显式授予的权限；
- 进程对其持有的 capability 集合是「显式声明 + 显式委托」的，不存在「默认对所有进程可见的全局命名空间」；
- 服务发现本身也是受控资源：谁能注册某服务名、谁能连接某服务名，都是 capability/声明约束的对象（类比 `unveil` 声明「我能连接哪些服务」）。

### 2.4 spawn 模型进程管理

MeuOS 用 spawn 模型（内建监督、资源账本 Day 1）替代传统 fork/exec。这直接影响 IPC：

- 新进程由服务管理器（spawn supervisor）按 manifest 拉起，manifest 可声明「该进程启动后导出哪些服务、连接哪些服务、拥有哪些 channel」；
- 按需激活（lazy activation）天然契合：lookup 一个服务名时若服务未运行，由 supervisor 按 manifest spawn 并注入其 listener endpoint capability；
- 进程退出 / 崩溃时，其持有的 capability 自动回收（内核级引用计数），无需用户态 broker 维护连接表。

### 2.5 Linux ABI 兼容层

兼容层（mkit）需要把 Linux 的各类 IPC 映射到 MeuOS 原语（详见第 4.6 节）。设计时必须保证映射是「表达力充分」的，否则会出现像某些微内核那样「shm 只能靠特殊驱动」的尴尬。

---

## 3. 业界方案对比

### 3.1 同步消息通道（seL4 Endpoint vs Zircon Channel）

| 维度 | seL4 Endpoint | Zircon Channel |
|------|--------------|----------------|
| 抽象 | 内核对象，由 capability 寻址；endpoint 内含「等待发送/等待接收」线程队列 | 内核对象；`zx_channel_create` 返回一对对称句柄（两端） |
| 同步模型 | 严格 rendezvous（会合）：`seL4_Send` 阻塞至被消费；`seL4_Call` 合并 send+recv，接收方阻塞在一次性 reply capability 上 | 通道本身 datagram 式；`zx_channel_write/read` 异步单向，`zx_channel_call` 提供同步请求-应答 |
| 载荷 | IPC buffer 中的消息寄存器（MR）；小消息走寄存器免拷贝（fastpath）；`seL4_MsgMaxLength` 限定最大长度 | 字节数据 + 句柄（handle）；消息为 datagram，含数据缓冲与 handle 数组 |
| 能力传递 | 一等公民：`cap transfer` 可在消息中转移 untyped/endpoint 等 capability；接收方指定 cspace 槽位 | 一等公民：消息可携带带权限（ZX_RIGHT_*）的 handle，经 `ZX_RIGHT_TRANSFER` 转移 |
| 多路复用 | Badge：`seL4_CNode_Mint` 给 endpoint capability 打 badge，内核随消息投递 badge，server 据此区分客户端 | 一个 channel 两端对称，多路复用通常由上层（多 channel / port）处理 |
| 优化 | IPC fastpath（仅 `seL4_Call`/`ReplyRecv`、消息入寄存器、无 cap 转移、无更高优先级线程时） | 无明确 fastpath 概念，但 call 路径为同步 RPC |

**取舍**：seL4 的 rendezvous + badge + reply capability 模型最干净、最契合 capability 安全，但其「小消息寄存器 + IPC buffer」双路径对大消息不友好；Zircon 的 channel 把「数据 + handle」统一进 datagram，且 `zx_channel_call` 直接提供同步 RPC，工程上更顺手。MeuOS 取二者之长：**rendezvous 语义（seL4）+ 数据/handle 统一的 datagram 消息（Zircon）+ call/reply 同步 RPC（Zircon）+ badge 多路复用（seL4）**。

### 3.2 异步事件通知（kqueue vs Linux epoll+eventfd+signalfd+timerfd）

| 维度 | kqueue（FreeBSD/macOS） | Linux epoll + eventfd + signalfd + timerfd |
|------|------------------------|--------------------------------------------|
| 接口数量 | 单一 `kqueue()` + `kevent()` | 四套：`epoll_create`/`epoll_ctl`/`epoll_wait`、`eventfd`、`signalfd`、`timerfd` |
| 事件源统一 | `(ident, filter)` 唯一标识；filter 含 READ/WRITE/SIGNAL/TIMER/VNODE/PROC/USER 等 | 各机制独立：fd 就绪（epoll）、用户计数（eventfd）、信号（signalfd）、定时（timerfd） |
| 辅助 fd | 不需要；事件以 ident（信号号、定时器 id、pid）直接区分 | 每个信号源/定时器/用户事件都需先 `signalfd`/`timerfd`/`eventfd` 创建一个 fd，再并入 epoll |
| 注册与取回 | `kevent()` 同一调用内先应用 changelist 再取 eventlist | 注册（epoll_ctl / signalfd 创建）与等待（epoll_wait）路径分离 |
| 状态管理 | filter 内建：`EV_CLEAR`（取后复位）、`EV_ONESHOT`、`EV_DISPATCH` | 用户态需自行管理 timerfd 重 arm、signalfd 消费等 |

**结论**：kqueue 的「统一 filter + 单结构体 + 单系统调用」天然覆盖 Linux 四套拼装，且避免 fd 碎片。这是「不重造概念但更干净」的最佳范例。MeuOS 的 EventPort 直接采用 kqueue 模型，并额外加入 `CHANNEL` filter（监听某个 channel endpoint 是否可读/可写/对端关闭），使「消息通道」与「事件通知」合流。

### 3.3 跨进程数据传递（Binder 对象引用/事务 vs 共享内存 + 自造同步）

| 维度 | Binder（Android） | 共享内存 + 自造同步（典型 Linux 做法） |
|------|------------------|----------------------------------------|
| 对象模型 | 对象引用（binder node / handle），跨进程传递的是「引用」而非裸指针；事务（transaction）携带节点标识 + 方法标识 + 参数副本 | 共享内存区域是「裸缓冲区」，需另造 futex/信号量做同步与就绪通知 |
| 事务语义 | 内建：sync 事务（带回复）与 oneway（单向）事务；内核驱动确定目标进程、复制事务、派发线程 | 需用户在共享内存之上自建协议与锁，正确性负担高 |
| 服务查找 | ServiceManager（用户态，但由 init 保证早启动）做名字→handle 解析 | 无内建；常依赖文件系统路径或外部配置 |
| 代价 | 所有事务经 Binder 驱动中转（一次内核拷贝），引用表维护复杂（flat_binder_object、binder_ref/binder_node） | 大块数据零拷贝，但同步原语复杂，且共享内存本身需额外 IPC 传递「就绪」信号 |
| 安全 | 事务带调用方身份（UID/PID），权限在驱动层校验 | 共享内存权限由文件/映射决定，跨进程身份需自行传递 |

**取舍**：Binder 的「对象引用 + 事务语义」对「服务间安全 RPC」极优，但其独立的驱动 + 引用表是复杂度来源；纯共享内存简单高效但同步负担全甩给用户。MeuOS 的立场：**主路径用 Channel 消息（小数据 + capability handle 传递），大数据用共享内存对象（也是一个 capability，经 Channel 传递其 handle），再用一条小消息通知「buffer 已就绪」**。这等价于 Binder 的对象引用 + 事务语义，但：
- 「对象引用」直接由 capability handle 表达（内核级、不可伪造），无需 flat_binder_object 的引用表；
- 「事务」直接由 Channel 的 call（sync，带回复）/ send（oneway，单向）表达；
- 「大数据零拷贝」由 SHM capability 提供，省去「自造 futex」——就绪通知复用 EventPort 的 CHANNEL filter。

### 3.4 服务发现（XPC/launchd 内核级注册激活 vs D-Bus 用户态 broker）

| 维度 | XPC / launchd（macOS） | D-Bus（Linux 主流） |
|------|------------------------|---------------------|
| 注册位置 | launchd（PID 1 体系）按 plist/manifest 管理；服务名在系统级注册，连接即按需激活（lazy launch） | 用户态 `dbus-daemon` broker；消息总线本身是一个「特殊应用」，转发所有消息 |
| 消息路径 | 连接建立后，XPC 连接（底层 Mach 端口）点对点直连，无 broker 中转 | 所有方法调用/信号均经 dbus-daemon 中转、匹配（match rules），多一次拷贝与单点 |
| 按需激活 | 内建：服务未运行则 launchd 拉起；空闲退出、崩溃重启 | 需配合 systemd 的 socket activation 等外部机制，非 D-Bus 自身能力 |
| 身份 | 连接带 audit token，内核可校验对端身份 | 依赖总线名声明与策略配置文件，策略在 broker 侧 |
| 内核参与 | 内核提供端口 + bootstrap 端口做名字解析 | 内核只提供 UNIX socket 传输，名字/路由全在用户态 |

**取舍**：D-Bus 把路由放在用户态 broker，带来中转开销与单点；XPC/launchd 的「内核/系统级名字解析 + 按需激活 + 直连」更干净。MeuOS 取 XPC/launchd 取向，但把「名字解析」下沉到内核 Service Registry（仍由 spawn supervisor 执行激活），连接建立后消息走 Channel 直连，无 broker 中转。

### 3.5 与 capability 安全的结合（贯穿性对比）

- **seL4**：endpoint 本身是 capability，badge 是多路复用标识，cap transfer 是消息一等公民——这是 MeuOS 的模板。
- **Zircon**：channel 两端是 handle（capability），消息携带带权限的 handle——与 MeuOS 一致。
- **Linux**：IPC 端点多以 fd 表达，权限来自文件权限位 / 命名空间 / socket 凭据（`SO_PEERCRED`）的多方拼凑，且 `SCM_RIGHTS` 传递 fd 的语法极不直观——这正是 MeuOS 要消灭的「不干净」。

---

## 4. MeuOS 选型建议

### 4.1 原语一：Channel —— 同步/带类型/可组合的消息通道

**抽象**：`channel_create` 创建一个内核对象，返回一对对称的端点 capability（endpoint A、endpoint B）。每个 endpoint 既可读也可写（全双工）。

**消息模型**：
- 一条消息 = 数据载荷（字节缓冲）+ 可选携带的 capability 句柄数组。
- 消息头含：`type tag`（可选的上层类型标记，用于「带类型」语义）、`data_len`、`num_handles`。
- 小消息走寄存器/快速路径免拷贝；大消息由内核在两端 IPC buffer 间边界拷贝一次。

**同步语义（rendezvous）**：
- `channel_send(ep, msg)`：阻塞直至对端接收（或投递到对端接收队列）。
- `channel_recv(ep, msg)`：阻塞直至有消息到达。
- `channel_call(ep, req, &reply)`：合并 send+recv，等价于「发送请求并阻塞等待该请求的回复」，对应 Zircon `zx_channel_call` 与 Binder 的 sync 事务。
- `channel_send_oneway(ep, msg)`：单向投递，不等待回复，对应 Binder 的 oneway 事务。
- 服务端用 `channel_reply` 向特定请求回送（reply 目标由内核维护的「reply capability」标识，对应 seL4 的 reply capability 思想），或 `channel_recv` + `channel_call` 循环服务多客户端。

**Badge 多路复用**：server 持有的 listener endpoint 可被 supervisor 用 `cap_mint` 派生出带 badge 的副本，分发给不同客户端；客户端发来的消息携带其 badge，server 凭 badge 区分来源，无需为每个客户端单独建 channel（对应 seL4 `seL4_CNode_Mint` + badge 投递）。

**Capability 传递（对象引用）**：消息可携带 capability handle（如另一个 channel endpoint、SHM 对象、甚至某服务名的连接 capability）。接收方在其 cspace 指定槽位接收。这直接实现 Binder 式「对象引用跨进程传递」，且引用由内核保证不可伪造。

### 4.2 原语二：EventPort —— 统一事件通知（kqueue 模型）

**抽象**：`eventport_create` 创建一个事件端口（capability）。`eventport_ctl(ep, op, filter, ident, fflags, udata)` 注册/修改/删除关注事件；`eventport_wait(ep, events[], timeout)` 取回就绪事件。

**统一 filter（单一 `(ident, filter)` 模型）**：

| filter | ident 含义 | 对应 Linux |
|--------|-----------|-----------|
| `EVFILT_READ` | channel endpoint / fd | epoll 可读 |
| `EVFILT_WRITE` | channel endpoint / fd | epoll 可写 |
| `EVFILT_CHAN` | channel endpoint（对端关闭、消息到达） | —（新增，原生融合 IPC） |
| `EVFILT_SIGNAL` | 信号号 | signalfd |
| `EVFILT_TIMER` | 定时器 id | timerfd |
| `EVFILT_PROC` | pid | —（进程事件，配合 spawn） |
| `EVFILT_USER` | 用户自定义 | eventfd |
| `EVFILT_VNODE` | 文件/vnode | inotify（未来） |

这样，**epoll + eventfd + signalfd + timerfd 四套被一个 EventPort 取代**，且不消耗额外 fd。`kevent` 结构（`ident/filter/flags/fflags/data/udata/ext`）原样采用。

### 4.3 原语三：共享内存对象（SHM）—— 大数据零拷贝通道

**抽象**：`shm_create(size, rights)` 创建一个共享内存对象（capability，带权限）。`shm_map(handle, ...)` 将其映射到本进程地址空间。

**用法**：大数据传输时，发送方 `shm_create` 并 `shm_map`，填入数据，然后将 SHM 的 capability handle 通过一条 Channel 消息（cap transfer）发给对端；对端 `shm_map` 后直接读取（零拷贝），随后用一条小 Channel 消息（或 EventPort 的 CHAN filter）通知「buffer 已就绪」。这等价于 Binder 大数据场景 + 自造同步，但同步信号复用统一的 EventPort，无需用户态 futex 协议。

### 4.4 原语四：Service Registry —— 内核级服务发现与按需激活

**抽象**：内核维护一个受 capability 约束的服务命名表。

- **注册**：服务进程用 `service_register(name, listener_ep, perms)` 将自身 listener endpoint 注册到名字 `name`。只有持有「该名字注册权限」的 capability 才能注册（防止抢占/仿冒）。
- **查找/连接**：客户端用 `service_connect(name)` 得到该服务 listener endpoint 的（badge 化）capability 副本，再 `channel_call` 发起请求。
- **按需激活**：若 `name` 已注册但对应进程未运行，spawn supervisor 按该服务的 manifest（声明可执行体、资源账本、导出能力）spawn 之并注入 listener endpoint；服务退出/崩溃后由 supervisor 回收其 capability 与注册项。这即 XPC/launchd 的 lazy activation，且连接建立后消息走 Channel 直连，**无 D-Bus 式 broker 中转**。
- **与 capability 安全融合**：服务名本身是可声明资源——进程 manifest 可 `unveil` 式声明「只允许连接 name X/Y、只允许注册 name Z」，未声明的名字 `service_connect` 直接拒绝。

### 4.5 与 capability-based 安全的端到端融合

- 所有四个原语的内核对象都是 capability；创建需要相应 `CREATE_*` 权限，使用需要持有对应 capability。
- Channel 消息中的 cap transfer 是「委托」而非「复制权限」：发送方可选 `cap_move`（转移后自身失效）或 `cap_copy`（派生副本），由 `ZX_RIGHT_TRANSFER` 类权限约束。
- Badge 提供不可伪造的来源标识，使「哪个客户端发的」成为内核保证事实，而非用户态自报。
- 服务名、SHM、EventPort、Channel 全部纳入进程的 capability 表与资源账本，崩溃即回收，无孤儿连接（对比 D-Bus broker 需维护连接表）。

### 4.6 Linux ABI 兼容层映射（mkit）

| Linux 机制 | MeuOS 原语映射 |
|-----------|---------------|
| pipe / fifo | Channel（单向用一端只读、一端只写派生） |
| AF_UNIX 套接字（含 `SCM_RIGHTS` 传 fd） | Channel（fd 即 capability handle，经 cap transfer 传递；`SCM_RIGHTS` 直接由消息携带 handle 实现，语义更干净） |
| eventfd | EventPort `EVFILT_USER` |
| signalfd | EventPort `EVFILT_SIGNAL` |
| timerfd | EventPort `EVFILT_TIMER` |
| epoll | EventPort 的 READ/WRITE filter |
| POSIX/System V 共享内存 | SHM 对象（capability） |
| futex（休眠/唤醒） | EventPort 的 USER filter 或最小兼容实现 |
| D-Bus 调用 | Service Registry + Channel call（兼容层可提供一个 libdbus 后端，用直连 Channel 替 dbus-daemon 中转） |

> 注：Windows 风格的命名管道 / mailslot 不在此映射范围；Linux 兼容层聚焦 mkit 子集。

### 4.7 相对 Linux 的简化论证（核心卖点）

1. **概念数量收敛**：Linux 十余种 IPC 机制 → MeuOS 四类原语（Channel / EventPort / SHM / Service Registry），且四类之间语义正交、可组合。
2. **权限模型统一**：Linux 用「文件权限位 + 命名空间 + socket 凭据 + SCM_RIGHTS」拼出权限；MeuOS 一律「端点即 capability」，权限显式授予、可委托、可撤销。
3. **事件通知合一**：epoll+eventfd+signalfd+timerfd 四套 → 单一 EventPort，无辅助 fd 碎片，注册/取回同一调用。
4. **对象引用原生**：Binder 式「对象引用 + 事务」由 cap transfer + channel_call/oneway 直接表达，无需 flat_binder_object 引用表与独立驱动 broker。
5. **服务发现无中转**：D-Bus broker 全量中转 → 内核 Service Registry 只做名字解析，连接后 Channel 直连，少一次拷贝与单点；按需激活由 spawn supervisor 内建。
6. **Cleaner by composition**：所有「干净」来自组合既有成熟概念（seL4 端点、Zircon channel、kqueue、XPC/launchd、Binder 引用语义），不引入任何新抽象。

---

## 5. 开放议题

1. **大数据零拷贝的边界**：SHM 适合「大块、可复用」数据；「中等体量、一次性」消息走 Channel 内核拷贝是否够快？是否需要引入 Zircon 式 FIFO/Stream 作为第三类传输以覆盖流数据？待原型基准测试。
2. **cap transfer 的拷贝语义**：`cap_move` 与 `cap_copy` 的默认策略如何与 Rust 所有权模型对齐？是否需要在 libc 层用 RAII 包裹 handle 生命周期，避免 capability 泄漏。
3. **EventPort 的 CHAN filter 语义细节**：对端关闭应触发 `EV_EOF`（类比 kqueue 的 `EV_EOF`）；多 writer 场景的「可写」判定阈值如何定义（半缓冲区？）需明确。
4. **Service Registry 与命名空间隔离**：多用户 / 多沙箱下，服务名是否按「域（realm）」隔离？容器/沙箱内 `service_connect` 是否应只看见本 realm 注册项？与 capability realm 的关系待定。
5. **reply capability 的生存周期**：`channel_call` 长事务中若客户端崩溃，reply 目标如何回收？是否需要（seL4 式）显式 `SaveCaller` 以支持并发服务多请求，还是用每请求独立临时 channel 简化模型？
6. **Linux 兼容层的 futex 语义**：futex 的「地址共享」语义（同物理页上的等待队列）与 MeuOS SHM+EventPort 模型存在映射缝隙，是否需要在兼容层保留一个最小的 futex 等价物？
7. **性能与 fastpath**：是否要为 `channel_call`（无 cap 转移、消息入寄存器）提供 seL4 式 fastpath 以压低服务调用延迟？混合内核下 fastpath 收益需实测。
8. **与调度器的耦合**：rendezvous 阻塞会触发上下文切换；在 spawn 监督模型下，server 线程池的唤醒策略（每请求一线程 vs 工作池）对延迟/吞吐的影响待原型验证。

---

## 6. 参考文献（真实 URL，已核验可访问）

### seL4（同步端点 / capability IPC）
- seL4 IPC 教程（endpoint、seL4_Call/ReplyRecv、badge、cap transfer、fastpath）：https://docs.sel4.systems/Tutorials/ipc.html
- seL4 Reference Manual（最新版 PDF，含端点对象与系统调用语义）：https://sel4.systems/Info/Docs/seL4-manual-latest.pdf
- seL4 文档站首页：https://docs.sel4.systems/

### Zircon / Fuchsia（channel、handle、能力传递）
- `zx_channel_create` 系统调用：https://fuchsia.dev/reference/syscalls/channel_create
- Zircon 系统调用索引（含 channel_read/write/call 等）：https://fuchsia.dev/reference/syscalls
- Zircon channel 内核对象说明：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/channel

### kqueue（统一事件通知）
- FreeBSD `kqueue(2)` 手册（filter 模型、`kevent` 结构、替代 epoll/eventfd/signalfd/timerfd）：https://www.freebsd.org/cgi/man.cgi?query=kqueue&sektion=2

### Binder（对象引用 / 事务语义 / 大数据通道）
- Android 开源项目 Binder 概览（客户端/服务器、代理/节点、事务、AIDL）：https://source.android.google.cn/docs/core/architecture/ipc/binder-overview?hl=zh-cn
- Android `Binder` 类参考：https://developer.android.com/reference/android/os/Binder

### XPC / launchd（内核级服务注册与按需激活）
- Apple XPC 文档（XPC service、launchd 按需激活、peer-to-peer 连接、audit token）：https://developer.apple.com/documentation/xpc
- Apple XPC `launchd` 接口：https://developer.apple.com/documentation/xpc/launchd

### D-Bus（用户态 broker 对照）
- D-Bus 规范（消息总线为特殊应用、bus names、类型系统、运行于用户态）：https://dbus.freedesktop.org/doc/dbus-specification.html

### Linux IPC（对照基线）
- `epoll(7)`：https://man7.org/linux/man-pages/man7/epoll.7.html
- `eventfd(2)`：https://man7.org/linux/man-pages/man2/eventfd.2.html
- `signalfd(2)`：https://man7.org/linux/man-pages/man2/signalfd.2.html

### OSDev（通用 IPC 综述）
- OSDev Wiki — Inter-Process Communication：https://wiki.osdev.org/Inter-Process_Communication

---

> 文档状态：规划 / 调研稿。待与「07-安全模型」「04-进程与调度」「09-启动与初始化」协同后定稿。

---

## 第三轮裁决回写（已采纳，自主决策）

> 本节为第三轮规划「查缺补漏」的裁决回写。以下将属于本文（05-IPC机制）的已采纳裁决以「已采纳」定稿追加，**仅追加、不删改**既有章节。呼应文件：`14-裁决-安全与命名空间.md`（C3）、`17-裁决-调试与兼容.md`（C9-A / C9-B）、`18-第二轮收敛摘要.md`（R 系）。

### 已采纳裁决 C3（Service Registry / 命名 与每进程 namespace 对齐）

- **采纳结论**：统一命名空间为每进程独立模型、内核无全局 `/`（见 14 §C3）；本文 §4.4 Service Registry 是「内核级服务命名表」，其命名与 08 的 Linux 路径树投影必须对齐——Service Registry 的 `name` 是 MeuOS 内部 capability 级名字，**不是全局文件系统路径**；兼容层把 Linux 路径树投影为每进程 namespace 绑定时，Service Registry 查询属于该绑定体系之一环。
- **对本文具体修订**：
  - §4.4 Service Registry「与 capability 安全融合」补一句：服务名本身是可声明资源，且**不与任何全局 `/` 耦合**；多 realm / 多沙箱下服务名按 realm 隔离（待 A7 拍板），`service_connect` 只可见本 realm 注册项。
  - §4.5 端到端融合点补：namespace 绑定可引用服务目录 cap；mount = 注入绑定，受 Service Registry + mount-capability 约束（呼应 14 §C3 连带影响）。
- **与 14 / 18 呼应**：本文 §5 待决 4（Service Registry 与命名空间隔离）据此收敛为「每进程 / 每 realm 视图」，与 C3 同构；残余仅 realm 边界粒度（A7）。

### 已采纳裁决 C9-A（调试权为 capability，守护 IPC 调试 / introspect 通道）

- **采纳结论**（细节在 `17`）：调试权是一组 capability（`DBG_*` 共 6 right，含拆出的 `DBG_POKE`）；生产态默认**全 deny**（mkit 与 glibc 一致）；dev/prod 差异靠 spawn / Job manifest 的 `debug_policy` 声明属性，**非全局开关**。
- **对本文具体修订**（仅当本文涉及调试 / introspect 通道时）：
  - 若 Service Registry 暴露「调试 / inspect 服务」发现，或新增任何 introspect 通道，必须经 `DBG_*` capability 守护：注册 / 连接调试服务需持对应 `DBG_*` right，默认 deny。
  - §4.5「所有四个原语的内核对象都是 capability」统一口径延伸至调试 introspect 端点：`DBG_*` 是 capability right 位图（07 §4.1）的扩展，IPC 调试接口受此守护，与本文「端点即 capability」原则一致。
  - 与 `12-调试与可观测性` 的 inspect 通道协同：inspect 读取归类 `DBG_INSPECT`（deny 默认），经 Service Registry 暴露的 inspect 端点同样受该 right 约束。

### 已采纳裁决 C9-B（异常传播与 EventPort 对齐）

- **采纳结论**（细节在 `17`）：异常事件内核一次性生成、copy 语义多播给监督者 + 持权调试器；监督者决策不可抑制，调试器仅收副本、有界冻结。
- **对本文具体修订**：
  - §4.2 EventPort 的 `EVFILT_PROC`（pid）filter 是与异常传播对接的入口：进程崩溃 / 退出经异常 channel 多播，监督者经 `DBG_CRASHDUMP` 订阅、调试器经 `DBG_ATTACH` 收副本；EventPort 本身只承载「事件就绪」通知，不承载异常决策权。
  - §4.1 Channel 若经 Channel 投递崩溃 dump / 异常（即 04 的 exception channel 复用 Channel 模型），其多播 + copy 语义遵循 17 C9-B，调试器持有的是副本且受 `DBG_ATTACH` 约束。
- **与 17 呼应**：异常多播的 right 校验（监督者 `DBG_CRASHDUMP` 默认订阅、调试器 `DBG_ATTACH` 仅副本）以 17 为准；本文只承接「异常事件经 IPC 原语投递」的传输层落点。

### 本文开放议题收敛提示

- §5 待决 4（Service Registry 与命名空间隔离）→ C3 收敛（每进程 / 每 realm 视图）；残余 realm 边界粒度待 A7。
- §5 待决其余（SHM 边界、cap transfer 拷贝语义、CHAN filter 细节、futex 语义、fastpath）与本轮裁决无直接冲突，维持开放待原型基准。

---

## 引用澄清（来自 24）

> 核验 `24-参考系统深度专项.md` §② 对 `01`–`12` 的引用失真清单（失真 1–7）。

- **结论**：`24` §② 的 7 条失真**全部指向 `06`（失真 1/2/3/5/7）与 `07`（失真 4/6）**，未对 `04` / `05` 提出任何失真修正。
- **一致性确认**：`24` 对本文的正面引用与本文一致、无需修正：
  - `24` §1.4 指明 `05` §3.1 借 seL4 rendezvous + badge + reply capability + fastpath、与 Zircon datagram 融合为 MeuOS Channel（`05` §4.1）——与本文 §3.1 / §4.1 一致。
  - `24` §3 决策映射表「seL4 IPC fastpath + badge + reply cap → `05` §3.1/§4.1 Channel」——一致。
  - `24` §4 自主点「Job 配额 + capability 边界 + 句柄表三者同源挂在 Job 上 Day 1……对应 `04` §4.4」——与本文 §4.4 一致。
- **处置**：`04` / `05` 无需据 `24` §② 做失真回写；仅在本文显式澄清，避免读者误以为存在未修正的引用偏差。
