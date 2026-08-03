# 64 - IPC 深化（Channel 类型矩阵 · zero-copy 大块传输 · 跨边界传递 · 与 capability mint/revoke 协同 · 双轨进程创建衔接 · 五架构同源）

> 独立调研一面（第八轮拓展·IPC 深化）· 调研 agent：lite / hy3
> 主题：在 `05-IPC机制.md` 四类原语（Channel / EventPort / SHM / Service Registry）之上深化 IPC 的(1) channel 家族类型边界 (2) VMO 句柄传递的 zero-copy 大块数据通路 (3) 跨 realm / 跨 Job 边界的 channel 传递规则 (4) 与 `07` capability mint / revoke 的精细粒度协同 (5) 与 C-G1 双轨进程创建（meu_spawn / meu_fork_light）的惯用法衔接 (6) 五架构同源论证。
> 性质：**纯新增独立调研文档**——百思纪律（只写设计/规划文档，不写实现代码；建立在 capability Day1（`07`）、Job 账本（`04` §4.4 / A5）、每进程 namespace（`06`，C3）、结构化取消（`04` §4.6 / A9）、异常多播（C9-B）等地基之上；不引入新内核原语除非必要；五架构同源）。**不改动 `00`–`63` 任何既有文件**（修订式追加纪律：仅在本文件内追加，结论如需回写由后续轮次执行）。
>
> 方法论铁律贯穿：解构型思维（先推演「应该是什么样」）、多系统参照、非缝合怪、修订式追加不删。

---

## 0. 一句话核心

> **MeuOS 的 IPC 是一族「纯 capability 内核对象」：MessagePipe（即 `05` 的 Channel）承载带类型/带句柄的 rendezvous 消息，DataPipe 补流数据、EventPair 补轻量信号、exception channel 复用同一机制投递结构化 CrashReport；所有端点经 handle transfer 跨进程传递，zero-copy 大数据走 VMO 句柄，跨 realm/Job 边界默认 deny 需显式授权，capability mint 收窄派生、revoke 触发对端 PEER_CLOSED，与 meu_spawn / meu_fork_light 双轨创建天然衔接，且五架构无任何语义差异。**

本文是 `05` 的深化层——地基（capability Day1、Job 账本、每进程 namespace、结构化取消、异常多播）一律复用，不重造；只补齐 `05` §5 已点名但未收敛的边界与协同问题。

---

## 1. 概述与目标

### 1.1 为何需要「深化」（本文相对 `05` 的定位）

`05-IPC机制.md` 已落定 IPC 的四类统一原语（Channel / EventPort / SHM / Service Registry），并给出选型理由。但 `05` §5 开放议题与相邻文档暴露出若干**未收敛的衔接面**，本文逐一深化：

| 衔接面 | 来源议题 | 本文落点 |
|--------|----------|----------|
| channel 家族的类型边界 | `05` §5 待决 1（是否需要 Zircon 式 FIFO/Stream 覆盖流数据） | §2 类型矩阵 |
| VMO 句柄传递的 zero-copy 大块数据通路 | `05` §4.3 / §5 待决 1（中等体量一次性消息边界） | §3 |
| 跨 realm / 跨 Job 的 channel 传递规则 | `05` §5 待决 4（Service Registry 与 realm 隔离）、A7 | §4 |
| capability mint / revoke 的精细粒度 | `07` §4.5（派生树 + revoke）、`05` §4.1（badge 多路复用） | §5 |
| 与双轨进程创建的衔接惯用法 | C-G1（已采纳 B 双轨，`47` §8）/ `04` §4.2 | §6 |
| 五架构同源 | `00` §5-6 / `59` §7.6 | §7 |

### 1.2 设计目标与约束（复用既有地基）

1. **复用 `05` 四原语**：本文不推翻 Channel / EventPort / SHM / Service Registry，只澄清与补强。
2. **capability Day1（07 §4.1）**：端点即句柄、权限随句柄走、可审计、可派生、可 revoke——IPC 深化层每一处都必须可被 capability 模型解释。
3. **百思纪律·不引入新原语除非必要**：`05` §5 待决 1 已明确提出「是否需要流传输」的疑问，故 DataPipe 的引入是被既有开放议题授权的、而非本文自创；EventPair 仅为轻量信号对，可由 event 对象家族派生，成本极低（见 §2.4 / 待决 P1/P2）。
4. **与结构化取消 / 异常多播同源**：exception channel 不是新概念，是 MessagePipe 机制在 `04` §4.6 / `59` §6（CrashReport + C9-B）上的复用。

---

## 2. Channel 类型矩阵（深化 `05` §4.1 / §5 待决 1）

### 2.1 四象限语义与边界

`05` 的 Channel 主范式覆盖「带类型、带句柄、rendezvous」消息，但 `05` §5 待决 1 已指出其边界：大数据块走 SHM、流数据是否需要第三类传输待定。本文把「Channel 家族」显式拆为四象限，明确各自语义与边界，避免错用：

| 类型 | 对应 `05` 原语 | 传输模型 | 是否携带句柄 | 方向性 | 典型场景 | 边界（不做什么） |
|------|----------------|----------|--------------|--------|----------|------------------|
| **MessagePipe** | `05` §4.1 Channel | datagram + rendezvous；call/reply 同步 RPC | 是（cap transfer） | 全双工、对称端点 | 服务 RPC、对象引用传递、启动即带 channel | 不做连续字节流（那是 DataPipe）；不做「仅信号无载荷」 |
| **DataPipe** | `05` §5 待决 1 的「流传输」 | 字节流（stream）、内核内单向或双向拷贝一次 | 否（纯字节） | 通常单向（可双向成 pair） | 大文件内容、stdout/stderr 管道、连续日志流、中等体量一次性负载 | 不携带句柄；非 rendezvous（写不阻塞等特定读者，只受缓冲区阈值） |
| **EventPair** | 轻量信号对（event 对象家族派生，`05` §4.2 同源） | 无载荷信号（signal / wait） | 否 | 双端对称，每端可读/可触发 | 轻量「对方还活着 / 请继续」握手；比 MessagePipe 更廉价的 ping/pong | 不承载消息、不承载句柄；不取代 EventPort（EventPort 是 kqueue 式多路复用器，EventPair 是点对点信号对） |
| **exception channel** | 复用 MessagePipe 机制 | datagram + copy 语义多播 | 是（CrashReport 句柄可选） | 由内核单向投喂监督者+调试器 | `04` §4.6 / `59` §6 致命异常上行、CrashReport 投递 | 不承载普通业务消息；消费者仲裁由 C9-B 定 |

**关键区分**：EventPort（`05` §4.2）是「多路复用事件收集器（kqueue 模型）」，EventPair 是「点对点信号对」——二者不竞争：EventPort 关注「我关注的 N 个源哪个就绪」，EventPair 关注「我与对端这一个握手信号」。四者共同建立在 `07` 的「内核对象 = capability」之上。

### 2.2 MessagePipe（= `05` §4.1 Channel，主范式不重述）

语义沿用 `05` §4.1：`channel_create` 返回一对对称端点 capability；消息 = 数据 + 可选句柄数组；`channel_send`/`recv`/`call`/`reply`/`send_oneway`；badge 多路复用（§5.1）。本文仅补强两点：

- MessagePipe 是**唯一**能携带 capability 句柄（cap transfer）的 channel 类型——这是对象引用传递（Binder 式语义）的落点（`05` §3.3 / §4.5）。DataPipe 与 EventPair 不携带句柄，因此**不能**用于能力委托。
- 大数据（可复用大块）不进 MessagePipe 载荷，改走 §3 的 VMO 句柄传递——MessagePipe 只传递「指向数据的 capability」，而非数据本身。

### 2.3 DataPipe（流数据，回应 `05` §5 待决 1）—— 推荐引入

**问题**：`05` §5 待决 1 明确指出「中等体量、一次性」消息走 MessagePipe 内核拷贝是否够快、是否需要 Zircon 式 FIFO/Stream 覆盖流数据，待原型基准。`05` 的 SHM（`§4.3`）适合「大块、可复用」，但对「一次性的连续字节流（文件内容、日志、stdout）」用 SHM 过重——需要建 VMO、map、通知、unmap，仪式感过强。

**方案**：引入 **DataPipe**（Zircon `zx_socket` 同构），作为 MessagePipe 的**流变体**：

- 内核对象，创建返回一对端点；字节从写端流入、内核在两端缓冲间拷贝一次、读端消费。
- 受 `WRITE`/`READ` rights 约束（同 `07` §4.1 基础 right）；不携带句柄。
- 半缓冲/全缓冲阈值定义「可写」判定（呼应 `05` §5 待决 3 的 CHAN filter 细节），溢出则写端阻塞或返回 `WOULDBLOCK`（经 EventPort `EVFILT_WRITE` 监听）。
- **定位**：覆盖 `05` §5 待决 1 的「流数据」空白；Linux 的 `pipe`/`AF_UNIX` 字节流、`stdout`/`stderr` 在兼容层（`08`）优先映射为 DataPipe（而非 MessagePipe），语义更贴。

**推荐立场（待决 P1）**：引入 DataPipe 是被 `05` §5 待决 1 显式授权的、填补既有开放议题的必要补强；不违反百思纪律（非自创新概念，Zircon socket 成熟对标）。但因其属「新增内核原语」，需大喵拍板确认 Day1/M2 落地阶段（建议 M2+，与 EventPort 同期，`47` §5.2）。

### 2.4 EventPair（轻量信号对，区别于 EventPort）

**问题**：某些场景只需「对端发个信号，我等它」的极轻握手（如「数据已就绪」「请退出」），用 MessagePipe 要构造消息头、用 EventPort 要建多路复用器——过度设计。

**方案**：引入 **EventPair**（Zircon `zx_eventpair` 同构），一对对称信号端点，每端可 `signal` / `wait`；无载荷、不携句柄；生命周期一端关闭即对端见 `PEER_CLOSED`（与 MessagePipe 同构，见 §5.2）。

**与 `05` §4.2 EventPort 的边界（重要）**：EventPort 是 `(ident, filter)` 多源多路复用器（kqueue 模型），EventPair 是单点对点信号。EventPair 不进 EventPort 的 filter 体系——它本身就是最朴素的「信号对」。EventPort 的 `EVFILT_USER` 可视为 EventPair 的「多路复用替代」，但 EventPair 在「仅两个主体握手」时更廉价、无需注册 filter。

**推荐立场（待决 P2）**：EventPair 可由 `07` event 对象家族派生（成本极低），且 Unix `eventfd` 语义在兼容层（`08` §4.6）可自然落为 EventPair。是否作为独立内核对象或 event 对象的特例，待大喵拍板；本文倾向「作为 channel 家族一员明确列出」，与 MessagePipe/DataPipe 形成对称的类型矩阵。

### 2.5 exception channel（复用 MessagePipe + CrashReport，呼应 `04` §4.6 / `59` §6）

- 异常上行**不是**新 channel 类型，而是 **MessagePipe 机制在结构化取消/异常域的复用**：内核统一故障入口（`59` §2.4 / §6.1）生成结构化 `CrashReport`，经 exception channel 以 **copy 语义多播**给监督者（`DBG_CRASHDUMP` 默认订阅）与持权调试器（`DBG_ATTACH` 仅收副本，有界冻结窗口，C9-B / `17` §3.2）。
- 监督者的 kill/restart 决策**不可被抑制**，调试器绝不可永久接管（`04` §4.6 / `47` §2.2）。
- 本文的增量：明确 exception channel 的「对端关闭」语义与 §5.2 的 revoke→PEER_CLOSED 一致——若监督者自身崩溃/被 revoke，调试器收到 `PEER_CLOSED`，而非静默丢失异常（呼应 `47` §7 议题 4 监督者故障域）。

### 2.6 类型选择指南（推荐立场）

| 需求 | 用 |
|------|----|
| 带类型 RPC、对象引用（传 capability 句柄）、启动即带 channel | **MessagePipe** |
| 连续字节流（文件/stdout/日志）、中等一次性负载 | **DataPipe** |
| 大块、可复用数据、零拷贝 | **SHM（VMO 句柄经 MessagePipe 传）**（§3） |
| 极轻量握手/信号（点对点） | **EventPair** |
| 多源事件就绪收集 | **EventPort**（`05` §4.2） |
| 致命异常上行 | **exception channel（复用 MessagePipe）** |

> 收口：四 channel 类型（MessagePipe / DataPipe / EventPair / exception-channel）+ SHM + EventPort + Service Registry，构成 MeuOS 完整 IPC 谱系；前四者是「channel 家族」，后三者是「配套机制」。家族内无重叠、边界清晰。

---

## 3. zero-copy 大块数据传输（深化 `05` §4.3）

### 3.1 VMO 即 capability，handle transfer 即授权（呼应 `07` §4.1）

`05` §4.3 已给出 SHM 用法：发送方 `shm_create` + `shm_map` 填数据，将 SHM 的 **capability handle** 经一条 MessagePipe 消息（cap transfer）发给对端，对端 `shm_map` 后直接读取（零拷贝），再以一条小消息通知「buffer 已就绪」。本文深化其 capability 协同面：

- VMO 是 `07` §4.1「内核对象」之一；其句柄携带 `READ`/`WRITE`/`MAP` 类 rights（`MAP` 为内存映射权，建议归入 `07` rights 位图，与 `EXECUTE` 正交——**待决 P3 关联 `03` 内存对象 rights 定稿**）。
- 「经 MessagePipe 传 VMO 句柄」= `05` §4.5 的 cap transfer = `07` §4.5 的「跨进程授予（grant via IPC）」：仅当发送方句柄含 `GRANT` right，且接收方在 cspace 指定槽位接收；接收方获得**受 `GRANT` 约束的权限**——即零拷贝能力授权，而非「共享内存无差别可见」。

### 3.2 与 `07` 句柄即 capability 的精细协同

- **权限随句柄走**：接收方拿到的 VMO 句柄 rights ≤ 发送方授予的 rights（`07` §4.5 派生只能收窄）。发送方传 `READ-only` 副本，接收方就只读——零拷贝不意味着零权限控制。
- **与 `07` 派生树（MeuOS 自研增强）协同**：VMO 句柄的派生关系记入 derivation tree（`07` 失真 4 澄清：派生树来自 seL4，是 MeuOS 在 Zircon handle 模型上的自研增强），revoke 任一派生节点即截断该分支（`07` §4.5 / §5.4）——见 §5.3。
- **与 47 §2.5 一致**：SHM 从不默认共享、必须显式经 capability 授予、受 rights 门控、生命周期由两端句柄引用计数管理（崩溃即回收，无孤儿）——本文不重述该不变量，只确认 zero-copy 通路不削弱它。

### 3.3 DataPipe vs SHM 的分工（避免「一切走消息」或「一切走共享」两极端）

- **DataPipe**：字节流、内核内一次拷贝、无需 map/unmap 仪式——适合「一次性流过、不需随机访问」的数据（stdout、日志、文件内容流式消费）。
- **SHM（VMO）**：零拷贝、需 map、可随机访问、可复用——适合「大块、重复读写、图形 framebuffer / AI 张量 / 音视频缓冲」（`47` §2.5 已论证其为 Day1 后一等公民）。
- 二者互补，覆盖 `05` §5 待决 1 的「边界」疑问：流数据 → DataPipe；大块可复用 → SHM。MessagePipe 本身只传「指向二者的 capability 句柄」，不传数据本体。

### 3.4 生命周期与崩溃回收（呼应 `07` §4.5 / `59` §4.4）

- VMO 内核对象的生命周期由**两端句柄引用计数**决定；任一方进程死亡，内核自动回收其句柄（含 VMO 句柄），引用计数归零即释放物理页（`07` §4.5 / `10` C8 撤销锁序列同构）。
- **崩溃即回收、无孤儿**：与 `10` C8「进程死亡自动回收全部句柄」一致；映射了该 VMO 的对端若仍持句柄，则 VMO 存活至最后引用释放，但崩溃方的写权随其句柄消失——避免「崩溃进程留下的共享内存被恶意复用」。

---

## 4. 跨 realm / 跨 Job 边界的 channel 传递规则

### 4.1 默认 deny：能力不跨边界（呼应 A7 多 realm 默认 deny / `14` §C3 每进程 namespace）

- **核心立场**：channel 端点本质是 capability 句柄；capability 模型（`07`）的「无隐式权限、权限显式授予」直接推出——**channel 不默认跨 realm / 跨 Job 边界可见**。这与 A7「多 realm 默认 deny」同源（`05` §5 待决 4 已点名 realm 隔离待 A7）。
- 一个进程只能向**已持有其句柄**的 channel 端点收发；要拿到跨边界的端点，必须由边界另一侧显式经 cap transfer 授予——而 cap transfer 本身受 `GRANT` right 与边界策略双重约束。

### 4.2 跨 Job（同 realm）：显式 handle transfer + `GRANT` right

- **同 realm 内跨 Job** 是 capability 的常规传递：父 Job 下的服务 A 经一条已建立的 MessagePipe 把某 channel 端点句柄传给另一 Job 下的 B（消息携带句柄），前提是 A 的句柄含 `GRANT`（`07` §4.5）。
- **账本归属（待决 P4）**：channel 内核对象（MessagePipe/DataPipe/EventPair 的内核态结构）的内存占用计到哪个 Job 的账本（`04` §4.4 五类资源）？选项：(a) 计创建者 Job，(b) 计当前引用方的「最浅公共祖先 Job」。跨 Job 传递后对象存活，但账本归属应稳定可审计——建议计**创建者 Job**（与 VMO 同源，引用计数独立于账本），但需 `04` owner 修订式追加定稿。
- 跨 Job 传 channel **不扩大**接收方的能力集：接收方只获得该端点句柄（及其 rights），不因此获得创建者 Job 的其他句柄——破 confused deputy（`07` §3.1 / `29` I5）。

### 4.3 跨 realm：需显式 cross-realm 桥接 capability，默认 deny（A7 待拍板）

- realm 是比 Job 更粗的隔离边界（A7「多 realm 默认 deny」）。跨 realm 传递 channel 端点**默认不可行**——即便两端各自持有 `GRANT`，realm 边界策略（默认 deny）拦截。
- **桥接机制（本文推荐，待 A7 拍板）**：引入一类**受约束的 cross-realm 桥接 capability**（类比 `07` §4.5 受约束 mint，仅限特定 channel 类型 / 特定对端 realm / 特定 rights 上限），由 realm 监督者持 `GRANT` 类权显式签发；未持桥接 cap 的进程无法把任何端点送出本 realm。
- 这与 `06`（C3 每进程 namespace）协同：realm 内 Service Registry 名字解析默认只可见本 realm 注册项（`05` §5 待决 4 / `47` §5.2）；跨 realm 服务发现须经同一桥接 cap 显式授权——名字不是越权凭证（呼应 `47` §2.4 / §4.4）。

### 4.4 与 `06` namespace 协同（名字发现按 realm 隔离）

- Service Registry（`05` §4.4）的 `name` 是 capability 级内部名字，**不耦合全局 `/`**（C3）；多 realm 下按 realm 隔离——`service_connect(name)` 仅返回本 realm 注册项，跨 realm 须桥接 cap（§4.3）。
- namespace 绑定可引用服务目录 cap；mount = 注入绑定，受 Service Registry + mount-capability 约束（`05` 第三轮回写 C3）——channel 传递规则与 namespace 视图在「默认 deny、显式授权」上同构。

---

## 5. 与 capability mint / revoke 的精细粒度

### 5.1 mint：收窄派生（badge 多路复用 + rights 收窄，呼应 `05` §4.1 / `07` §4.5）

- **badge 多路复用**：server 的 listener endpoint 由监督者用 `cap_mint` 派生带 badge 的副本分发给不同客户端（`05` §4.1）；badge 是内核保证的不可伪造来源标识，客户端消息携带 badge，server 凭以区分来源——无需每客户端建独立 channel。
- **rights 收窄**：mint 出的副本 rights ≤ 原句柄（`07` §4.5「派生只能收窄」）；如 server 给某客户端只 mint `READ`+`WRITE` 的 channel 端点副本，该客户端无法再 mint 给他人（无 `GRANT`/`DUPLICATE`）。
- **受约束 mint（防 confused deputy）**：与 `07` 第四轮回写 C2 / `29` I5 同构——mint 委托人（如 devmgr / supervisor）持「限定 channel 类、限定 Job 子树」的受限 mint right，`rights_issued = rights_service ∩ rights_client_requested`；杜绝恶意客户端借 mint 拿到超范围权。

### 5.2 revoke：对端 EOF / PEER_CLOSED 语义（深化 `07` §4.5 + `04`/`59` exception channel）

- **revoke = 句柄回收 + 对端通知**：revoke 一个 channel 端点句柄（或进程死亡自动回收，或显式 `close`/`REVOKE`），不仅从本进程句柄表移除，内核还向**对端端点**投递 `PEER_CLOSED`（Zircon `ZX_ERR_PEER_CLOSED` 同构）——对端后续收发立即失败、EventPort `EVFILT_CHAN` / `EVFILT_READ` 触发 `EV_EOF`（呼应 `05` §5 待决 3）。
- **这与「端点即 capability」深度协同**：revoke 不是「悄悄删表项」，而是 capability 模型里「权限撤回」在双向对象上的必然表现——撤回一端，另一端立即感知连接失效，不会向已消失的对端静默写入（破「孤儿连接」类 bug，`05` §4.5）。
- **exception channel 同构**：§2.5 的 exception channel 若监督者被 revoke，调试器同样收到 `PEER_CLOSED`，异常投递失败可感知（`47` §7 议题 4）。

### 5.3 派生树下的 revoke 粒度（MeuOS 自研增强，呼应 `07` 失真 4 澄清）

- `07` 失真 4 澄清：派生树来自 seL4，是 MeuOS 在 Zircon handle 模型上的**自研增强**（Zircon 本身无派生树）。这带来精细 revoke 粒度：
  - 若 server mint 了 100 个带 badge 的客户端副本（§5.1），revoke **某一个**客户端副本只切断该客户端（对端 PEER_CLOSED 仅作用于该副本对应的对端），不影响其余 99 个——这是派生树支持的「定点 revoke」。
  - revoke 根 listener 端点则级联切断其全部派生（按 derivation tree 传播，`07` §4.5 / §5.4 revoke 闭环）。
- **推荐立场**：派生树 revoke 是 MeuOS 相对 Zircon 的差异化能力优势，应在 IPC 层显式利用——server 可「按客户端 badge 精确撤回」，无需重建整条 channel。性能/内存成本（派生树记录）维持 `07` §5 待决 4 的「原型期显式 close、推迟 derivation 追踪」默认，但 IPC 层的定点 revoke 语义从 Day1 即定义清晰。

### 5.4 推荐立场

- revoke 必触发对端 `PEER_CLOSED` 是**不可选不变量**（与 capability 模型同源），Day1 即落地。
- 定点 revoke（派生树）的**语义** Day1 定义，**实现**可随 `07` §5 待决 4 逐步从「显式 close」演进到「全量派生树追踪」。
- 是否需要向对端附带「revoke 原因」（权限撤回 / 对端崩溃 / 显式关闭）作为 `PEER_CLOSED` 的附带载荷——**待决 P5**（设计细节，可自决，建议附带原因码以利调试）。

---

## 6. 与 C-G1 双轨进程创建衔接（深化 `04` §4.2 / `47` §2.6 / §8 C-G1）

### 6.1 meu_spawn：handle transfer list 作「启动即带 channel」的惯用法

C-G1（第七轮精细裁决，已采纳 B 双轨，`47` §8）将 `meu_fork_light` 提升为与 `meu_spawn` 并列的原生 ABI。本文衔接二者与 IPC 的「启动即带 channel」惯用法：

- `04` §4.2 `meu_spawn()` 输入第 3 项即**句柄继承集（handle transfer list）**——父进程显式列出哪些句柄（channel / event / VMO / device…）传给子进程，语义为 move / duplicate-with-rights，**默认不继承任何句柄**（最小特权）。
- **惯用法 A（服务启动即带客户端通道）**：服务 A 启动时，监督者（`meu-supervisor`，`04` §4.5）经 `meu_spawn` 的 handle transfer list 把「与 A 通信的 MessagePipe 客户端端点 + 其 stdout DataPipe + 必需的 VMO 句柄」注入 A；A 从入口点起即持有这些 channel，无需启动后再 `service_connect` 握手。
- **惯用法 B（Service Registry 按需激活注入 listener）**：`05` §4.4 的 lazy activation——lookup 未运行的服务时，supervisor 按 manifest `meu_spawn` 之，并把该服务的 **listener endpoint capability** 经 handle transfer list 注入，连接建立后走 MessagePipe 直连、无 broker 中转。

### 6.2 meu_fork_light：句柄表快照继承（默认-deny 但含父快照通道）

- `47` §2.6 / `47` §8：meu_fork_light 从自身快照派生，**默认继承父快照时刻的句柄表与 namespace 模板（默认-deny，可显式裁剪）**，单线程限定、归入父 Job。
- **惯用法（shell 管道）**：shell 经 `meu_fork_light` 派生子进程执行 `cmd1 | cmd2`——子进程继承父（shell）持有的 stdin/stdout DataPipe 端点句柄，管道自动连通，无需 spawn 时显式列 handle list。这是兼容层（`08`）`fork` 直接映射 `meu_fork_light`（`47` §8 微调项）后，`popen`/`pipe` 类语义的自然落点。
- **安全不变量零退让**：fork_light 子进程继承的是「父快照时刻、已持有、默认-deny」的句柄集——它不因此获得任何父未持有的权；合成 uid 零权限（`04` C4）。

### 6.3 双轨对比与推荐惯用法清单

| 场景 | 推荐原语 | 启动即带 channel 方式 |
|------|----------|------------------------|
| 从零服务、需精确 capability | `meu_spawn` | handle transfer list 显式注入（惯用法 A/B） |
| 语言运行时派生、shell 管道、继承当前态工具 | `meu_fork_light` | 继承父快照句柄集（含管道端点） |
| 按需激活未运行服务 | `meu_spawn` + Service Registry | supervisor 注入 listener endpoint |
| 调试/观测通道 | `meu_spawn` 带 `DBG_*` 受限句柄 | 监督者持 `DBG_CRASHDUMP` 默认订阅（C9-B） |

> 收口：channel 传递的「源头」只有两条——(1) 经已建立的 MessagePipe 做 cap transfer（运行时）；(2) 经 spawn / fork_light 的句柄继承集（创建时）。二者都落入 capability 模型，无第三条隐式路径（呼应 `07` §4.5「无隐式提升」）。

---

## 7. 五架构同源（深化 `00` §5-6 / `59` §7.6）

### 7.1 IPC 是纯能力/对象，无架构差异

- MeuOS 目标五架构（x86_64 / aarch64 / riscv64 / loongarch64 / arm，`00` §5-6 / `22` §8）。IPC 原语（MessagePipe / DataPipe / EventPair / SHM / EventPort / Service Registry）全部是**架构无关的 capability 内核对象**：句柄表、rights 位图、channel rendezvous 语义、cap transfer、revoke→PEER_CLOSED、派生树——五架构逐字一致，无任何架构特化语义。
- 这与 `59` §7.6「统一中断 cap 面 + 每架构薄后端」同源：中断对象已证成「cap 面五架构一致、薄后端只承载寄存器差异」，IPC 是更纯粹的「纯对象」——连寄存器差异都没有，只有 syscall 入口 stub 差异。

### 7.2 每架构薄后端只承载 syscall 入口 stub

- 五架构差异仅在于** syscall 进入方式**（x86_64 `syscall`/`int`、aarch64 `svc`、riscv64 `ecall`、loongarch64 `syscall`、arm `svc`）与参数寄存器约定；进入内核后，IPC 全部走同一套 Rust 实现的对象状态机（`07` §4.4 最小 TCB，Rust 承载安全关键路径）。
- 句柄表布局、rights 校验、channel 队列——同一份架构无关代码，五架构共享单测（`00` §5-6「TCB 单测五架构复用、仅后端薄层每架构验证」）。
- i386 / arm 32 位纯 C 退化面（关联 `00` C6 / `29` D-T6）对 IPC 的影响仅限入口 stub，不侵蚀对象模型（`59` §7.7 同构）。

### 7.3 推荐立场

- IPC 五架构同源是**已证成不变量**（与中断对象同构，无新增成本）；`23-ABI稳定性与版本.md` 的 ABI 契约须显式声明「IPC 系统调用语义五架构一致」，避免兼容层（`08`）逐架构特化（待决 P6，交 `23` owner 修订式追加）。

---

## 8. MeuOS 推荐（综合定稿）

### 8.1 核心推荐

1. **Channel 家族四象限定型**：MessagePipe（`05` Channel，主范式、唯一可携句柄）、DataPipe（流数据，回应 `05` §5 待决 1，**推荐引入**）、EventPair（轻量信号对，区别于 EventPort）、exception channel（复用 MessagePipe + CrashReport，呼应 `04` §4.6 / `59` §6）。四者边界清晰、无重叠。
2. **zero-copy 大块数据走 VMO 句柄**：经 MessagePipe cap transfer 授权，与 `07` 句柄即 capability 协同——权限随句柄走、派生只能收窄、崩溃即回收、无孤儿；DataPipe 覆盖流、SHM 覆盖大块可复用，MessagePipe 只传指向二者的句柄。
3. **跨边界默认 deny**：channel 端点即 capability，不默认跨 realm/Job 可见；跨 Job 同 realm 经 `GRANT` + 显式 cap transfer；跨 realm 需 cross-realm 桥接 cap（A7 默认 deny，待拍板）。与 `06` C3 每进程 namespace 同构。
4. **mint 收窄 / revoke 触发 PEER_CLOSED**：badge 多路复用 + rights 收窄（防 confused deputy）；revoke 必通知对端 `PEER_CLOSED`（不可选不变量）；派生树支持定点 revoke（MeuOS 自研增强，`07` 失真 4）。
5. **双轨创建天然衔接 IPC**：`meu_spawn` 经 handle transfer list 注入启动 channel（含 Service Registry 按需激活 listener）；`meu_fork_light` 继承父快照句柄集（shell 管道）；C-G1 已采纳双轨，本文补齐其 IPC 惯用法。
6. **五架构同源无差异**：IPC 是纯能力/对象，薄后端只承载 syscall 入口 stub；与 `59` §7.6 中断对象同构，无新增成本。

### 8.2 与既有地基的对齐（零冲突、纯深化）

- 与 `05` 四原语完全一致（本文只澄清/补强 `05` §5 待决 1、待决 3、待决 4）；不推翻 Channel/EventPort/SHM/Service Registry。
- 与 `07` capability 模型（句柄即 cap、受约束 mint、派生树、revoke、`GRANT`/`REVOKE`/`DUPLICATE`）完全同构。
- 与 `04` §4.2（句柄继承集）、§4.4（Job 账本）、§4.5（监督）、§4.6（异常上行）一致；与 A5 / A9 / C3 / C4 / C9-B 全部衔接。
- 与 `47`（C-G1 双轨、SHM 一等、scoped 标识、well-known name）一致；不引入 `47` 之外的进程模型概念。
- 与 `59`（exception channel、CrashReport、C9-B 多播仲裁）一致，仅把 IPC 侧显式化。
- 与 `00` §5-6 / `22` §8 五架构同源一致。

---

## 9. 待决项表（P1–P6）

| 编号 | 待决项 | 关联文档 | 默认推荐立场 | 需大喵拍板？ |
|------|--------|----------|--------------|--------------|
| P1 | **DataPipe 是否引入为独立内核原语**（流数据传输，`05` §5 待决 1） | `05` §4.1/§5-1、`08` §4.6（pipe/AF_UNIX 映射） | 引入，M2+ 与 EventPort 同期；Zircon socket 成熟对标，属填补既有开放议题 | **是**（新增原语，百思纪律要求确认） |
| P2 | **EventPair 是否列为独立内核对象，还是 event 对象特例** | `05` §4.2、`07` event 对象家族、`08` §4.6（eventfd） | 列为 channel 家族一员；成本极低（event 派生） | **是**（与 P1 同属类型矩阵收口，建议合并拍板） |
| P3 | **VMO 的 `MAP` right 在 `07` rights 位图的定稿位置** + 跨 realm 桥接 cap 模型与 realm 边界粒度（A7） | `07` §4.1/§4.5、`03`、`06` C3、A7 | `MAP` 归入内存对象 rights（与 `EXECUTE` 正交）；跨 realm 默认 deny + 显式桥接 cap | **是**（A7 本身即待拍板；MAP 位定稿交 `07`/`03` owner） |
| P4 | **跨 Job channel 内核对象的账本归属**（创建者 Job vs 最浅公共祖先 Job） | `04` §4.4（五类资源账本）、`07` | 计创建者 Job，引用计数独立于账本（与 VMO 同源） | 否（由 `04` owner 修订式追加定稿，不阻塞 Day1） |
| P5 | **revoke 触发 PEER_CLOSED 时是否附带「原因码」**（权限撤回/对端崩溃/显式关闭） | `07` §4.5、`04`/`59` exception channel | 附带原因码，利于调试与 `EV_EOF` 语义 | 否（设计细节，可自决；建议 Day1 带原因码） |
| P6 | **IPC 系统调用语义五架构一致的 ABI 契约显式声明**（防 `08` 逐架构特化） | `23-ABI稳定性与版本`、`00` §5-6、`22` §8 | 在 `23` 契约中加入「IPC 语义五架构一致」条款 | 否（交 `23` owner 修订式追加） |

> 待拍板项汇总：P1、P2、P3 需大喵拍板（其中 P3 含 A7 这一既有待拍板项）；P4/P5/P6 可由对应文档 owner 以修订式追加定稿，不阻塞 Day1。

---

## 10. 参考文献（与 `05`/`04`/`07`/`47`/`59` 共用权威来源，不重复罗列；此处仅列本文深化视角）

- Fuchsia Channel（datagram + handle transfer + call/reply）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/channel
- Fuchsia Socket（stream / DataPipe 对标）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/socket
- Fuchsia EventPair（轻量信号对）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/eventpair
- Fuchsia Event（信号对象，EventPair 同源）：https://fuchsia.dev/fuchsia-src/reference/kernel_objects/event
- Zircon Rights（句柄即 capability、rights 位图）：https://fuchsia.dev/fuchsia-src/concepts/kernel/rights
- seL4 IPC / CSpace（capability 派生树、revoke 传播）：https://docs.sel4.systems/Tutorials/ipc.html
- macOS Mach exception model（异常 = 消息，CrashReport 思想源头）：https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KernelProgramming/Exceptions/Exceptions.html
- `05-IPC机制.md`（Channel/EventPort/SHM/Service Registry、§5 开放议题）、`04-进程与调度.md`（§4.2 句柄继承集 / §4.4 账本 / §4.5 监督 / §4.6 异常上行）、`07-安全模型.md`（§4.1 句柄即 cap / §4.5 mint·revoke·派生树 / 失真 4 澄清）、`47-进程管理-方案B.md`（§2.3 Channel / §2.5 SHM / §2.6 meu_fork_light / §8 C-G1）、`59-中断与异常子系统深化.md`（§6 exception channel / C9-B 多播仲裁）、`00-总览与路线图.md`（A5/A9/C3/C4、§5-6 五架构同源）、`06-文件系统.md`（C3 每进程 namespace）、`14-裁决-安全与命名空间.md`（C3）、`17-裁决-调试与兼容.md`（C9-B）、`56-核心子系统现代化方案对比与合成.md`（C-G1 冲突 1）、`23-ABI稳定性与版本.md`（IPC ABI 契约）

---

> 关联索引：本文为第八轮拓展「IPC 深化」独立调研面，纯新增 `64`；深化 `05`/`04`/`07`/`47`/`59`，与既有 `00`–`63` 零冲突。结论如需回写（如 DataPipe/EventPair 进 `05` 类型矩阵、VMO `MAP` right 进 `07` 位图、跨 realm 桥接 cap 进 A7 裁决、IPC 五架构一致条款进 `23`），由后续修订轮次以「修订式追加」执行，本文件不改 `00`–`63` 任何内容。
