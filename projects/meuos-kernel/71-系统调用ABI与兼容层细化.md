# 71 - 系统调用 ABI 与兼容层细化（Syscall ABI & Compatibility Layer Refinement）

> 子领域：系统调用 ABI 契约 / Syscall ABI 与兼容层边界。
> 团队：`kernel-plan`（拓展维度调研员，lite / hy3 产出）。
> 关联文档：`00-总览与路线图`、`05-IPC机制`、`07-安全模型`、`08-Linux兼容层`、`11-自举验证方案`、`21-迁移路线图`、`23-ABI稳定性与版本`，以及 `63-网络栈实现深化`（mkit 映射范式）、`06-文件系统`（`.msys` 原子更新）、`04-进程与调度`（Job 账本 / meu_spawn）。
>
> **百思纪律（铁律）**：本文只写设计/规划文档，不写实现代码；结论可修订式追加。本文建立在以下已决地基之上——capability Day 1 唯一权限模型（`07`）、内核态 mkit 薄层（`08` §1.2 / `00` A10）、`.msys` 同版本升级杠杆（`23` §5 / `06` §4.3）、ABI 稳定性三层分级（`23` §2）、用户可见结构体布局冻结于 `23` ABI 稳定性纪律、**五架构（x86_64/i386/aarch64/riscv64/loongarch64）syscall 逻辑同源**（`21` §5 / `11` §5）；**不引入新内核原语、不新增 right bit**（rights 位图冻结 bit 0–47，`07` §4.1，bit 48+ 预留）。
>
> 方法论铁律贯穿：解构型思维、多系统参照（Linux syscall 表 / Fuchsia syscall / Zircon syscall / seL4 invoke / Windows NT syscall）、非缝合怪、修订式追加不删。
>
> 本文为规划草案，后续以修订式追加更新，不覆盖、不删改 `00`–`70`、README、`.issues/`、git。跨域项以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准；与 `08`/`23`/`05`/`07`/`11`/`21` 严格一致。

---

## 0. 核心结论（一句话）

> **五架构共用一套统一 syscall 编号（不在每架构分号段），syscall 入口经内核态 mkit 薄层把「类 Linux syscall 语义」翻译为「MeuOS native capability 调用」；errno 语义保持 POSIX 兼容；用户可见结构体布局冻结于 `23` ABI 稳定性纪律；syscall 版本化随 `.msys` 版本演进；native 路径不经兼容层、直接发 capability 调用（与 `05`/`07` 衔接）。**

---

## 1. 五架构 syscall 编号方案（统一编号 + 入口分发；与 `11`/`21` 衔接）

### 1.1 统一编号：单一线性名字空间，不分架构号段

> **方案**：MeuOS 采用**单一、跨五架构统一**的 syscall 编号空间——所有架构（x86_64/i386/aarch64/riscv64/loongarch64）对同一个「操作」使用**同一个 syscall 号**，内核态 mkit 薄层持**一张统一分发表**（`00` A10 / `08` §1.2）。

解构参照：Linux 在每架构各自维护一套 syscall 表，且 x86_64 用 `0x40000000 + nr`（`__X32_SYSCALL_BIT`）之类的号段切分 32/64 位——这是「每架构独立演进、相互不协调」的历史产物。MeuOS 因五架构**同源自研**（`21` §5 / `11` §5），无此历史包袱，应直接采用单一线性编号，避免「同语义不同号」造成的 libc/兼容层碎片。

- **编号域**：建议 32 位无符号整型编号（与五架构寄存器宽度兼容，预留充足扩展位）；bit 高位不用于区分架构，只用于区分「native vs compat」语义子集（见 §4）。
- **分发表位置**：统一 syscall 分发表是**架构无关数据**，由内核态 mkit 薄层持有；每架构仅贡献一个极薄的**入口蹦床（per-arch trampoline）**，把各自 trap 约定（x86_64 `syscall` / i386 `int 0x80` 或 `sysenter` / aarch64 `svc #0` / riscv64 `ecall` / loongarch64 `syscall`）与**参数寄存器布局**归一化为内核内部统一的「syscall frame」（`21` §5.3 i386 走纯 C 快通道，入口蹦床亦走 C 边界）。
- **同源收口**：分发逻辑、号→原语映射、errno 合成全部在统一层实现一次；五架构只差「trap 指令 + 参数寄存器 ABI 归一」，差异经 per-arch 蹦床屏蔽（`21` §4 统一链接脚本合并同源内核镜像，`11` §5 五架构自举验证）。

### 1.2 入口分发链路

```
[每架构 trap]  x86_64 syscall / i386 int0x80 / aarch64 svc / riscv64 ecall / loongarch64 syscall
      │
      ▼
[per-arch trampoline]  参数寄存器 → 内核统一 syscall frame（归一化）
      │
      ▼
[内核态 mkit 薄层]  ① 取统一 syscall 号  ② 查统一分发表  ③ 分发
      │
      ├─(native 子集)─────────────▶ 直接 native capability 调用（§5 快路径，近零翻译）
      │
      └─(compat / Linux 子集)─────▶ 兼容层翻译（§2）：类 Linux 语义 → 内部 capability/IPC/VMO/Job 原语
                                          │
                                          ▼
                                    native capability 调用（终态与 native 路径同构）
```

- 关键：无论 native 还是 compat 入口，最终都收敛到**同一套 native capability 调用**（`05`/`07`）。兼容层只是「语义翻译面」，不是「另一条内核路径」。
- 与 `11` 衔接：M0–M4 自举验证的 syscall 子集（read/write/open/close/mmap/基础进程/基础信号，`11` §5）是统一编号表的**首批冻结项**；与 `21` 衔接：meuos-libc 的 `syscall gate`（`21` §2 / §5.3）面向统一编号发出 syscall，保证五架构 libc 同源。

### 1.3 五架构差异边界（仅入口蹦床，分发同源）

| 架构 | trap 指令 | 参数寄存器 ABI | 入口归一（薄后端） |
|------|-----------|----------------|--------------------|
| x86_64 | `syscall` | rdi/rsi/rdx/r10/r8/r9 | 归一为内部 syscall frame |
| i386 | `int 0x80` / `sysenter` | ebx/ecx/edx/esi/edi/ebp | 纯 C 快通道（`21` §5.3），无 Rust 裸机 target |
| aarch64 | `svc #0` | x0–x5 | 归一为内部 syscall frame |
| riscv64 | `ecall` | a0–a5 | 归一为内部 syscall frame |
| loongarch64 | `syscall` | a0–a5 | 归一为内部 syscall frame（Day1 后移，`11` §5-4） |

> 论断：syscall **编号与分发逻辑全同源**；差异只在「trap 指令 + 参数寄存器布局」的 per-arch 归一蹦床，该蹦床不含策略、只做整形，严格对齐 `21` 五架构代码生成 + `11` 五架构自举验证。

---

## 2. mkit 兼容层定位与边界（Linux syscall → native capability call 映射）

### 2.1 兼容层定位：语义翻译面，非权限源

> 继承 `08` §1.2 / §1.3 铁律：**兼容层是「类 Linux syscall 语义 → MeuOS native capability 调用」的薄翻译面，不是新的权限权威**。

- **权威只来自内部 capability**（`08` §1.3 原则 1）：兼容层把 Linux 二进制请求翻译成对「已持有的 capability 句柄」的操作；二进制能做什么，由它所在 Job 的 capability 集合（`07`/`04`）决定，与「它以为自己是 root」无关（uid 0 是合成视图，`08` §3.1 / `C4` 已采纳）。
- **能直连内部原语就不仿真**（`08` §1.3 原则 3）：mkit direct-mapped 子集（`08` §2.2）的 syscall 号与内部原语 **1:1 对应**，薄层仅做参数整形 + errno 合成，是入口分发表的直接分支，无额外语义层。
- **翻译失败即 `ENOSYS`/EPERM 返回，不悄悄降级**（`08` §1.3 原则 4）：子集外 / 缺 capability 一律显式不兼容，杜绝「看起来能跑实则错」的缝合怪。

### 2.2 映射总览：Linux syscall → native capability call

```
Linux 二进制 / mkit 程序 发 syscall（统一编号）
      │
      ▼
内核态 mkit 薄层（统一分发表）
      │  ── direct-mapped 子集（mkit / Phase0）──▶ 1:1 → native capability call（极薄）
      │
      ▼
兼容层（翻译面）—— emulated / synthesized 子集（Phase1–3）
   · 意图翻译：Linux 语义 → 内部原语调用（capability / IPC / VMO / Job）
   · 语义合成：内部结果 → Linux errno / 返回值 / 信号投递
      │
      ▼
native capability 调用（终态，与 native 路径 §5 同构）
```

- **direct-mapped 子集**（`08` §2.2 / §2.4）：`open/read/write/close/mmap/meu_spawn` 等，薄层仅整形参数 + 合成 errno，本质是「native 路径的兼容别名」——这部分**在 kernel 薄层内完成、不经用户态仿真**，是 `00` A10「内核态 mkit 薄层吸收宏内核 syscall 零往返优势」的具象。
- **emulated 子集**（`08` §2.2）：`fork`/`execve`/信号全集等，由薄层转发至用户态服务（监督者 / 兼容运行时）翻译为 `meu_spawn` + Job 账本克隆 + exception channel 映射（`08` §3.3 / §3.4，`04` §4.5）。
- **synthesized 子集**（`08` §2.2）：`/proc`/`/sys`/`/dev` 伪文件，由统一命名空间在查询时生成 Linux 视图（`08` §4.1 / `06` C3 已采纳），不落地真实文件。

### 2.3 兼容层边界（与 `08` 内核态 mkit 薄层协同）

- **内核薄层内部分**：仅含 (a) 统一分发表，(b) direct-mapped 子集的 1:1 整形 + errno 合成，(c) emulated/synthesized 子集的「转发路由」。这三部分**只做机械映射，不含安全策略**——权限判定发生在下游 native capability 调用处（`07` rights 校验）。
- **用户态兼容运行时**：emulated/synthesized 的复杂语义（fork CoW、信号投递、伪文件生成）在用户态完成（`08` §5.6 边界：「兼容层是用户态蹦床，不是内核 ABI 后门」）。
- **与 `08` §1.2 同构**：薄层四类职责（分发 / per-task Linux Context / 意图翻译 / 语义合成）中，「权限裁决」不在薄层——它只消费 capability，不发放（契合 `08` §4.4 与 `07` 协同）。
- **边界红线**：兼容层**不得**提供任何「绕开 capability 模型」的逃生舱；`NET_*`/`DBG_*` 等服务能力仍由 `07` rights 位图约束，兼容层只是把这些 right 暴露为 Linux 二进制可理解的「fd / 错误码」表面。
- **是否进入 TCB**：direct-mapped 整形逻辑因处于内核态、属 dispatcher 一部分，**默认纳入内核 TCB 边界**；但因其无策略、仅整形，TCB 增量极小。emulated/synthesized 的复杂语义退出内核、降为可重启用户态组件（`00` A6 交权缩 TCB）。「兼容层整体是否算 TCB」为待决项（§7 D2）。

---

## 3. errno 与用户结构体语义（POSIX 兼容面；布局冻结于 `23`）

### 3.1 errno 语义：POSIX 兼容表面

> **方案**：对 Linux 二进制，syscall 采用 **POSIX 兼容 errno 约定**——返回约定与 Linux 一致（`-1` + `errno` 全局/每线程变量，或负 errno 内联返回，由 libc gate 决定）；内部 native capability 调用的失败（rights 缺失 / 句柄无效 / revoke）统一映射到 POSIX errno 集合。

- 内部失败 → errno 映射表（设计协同，§7 D3）：

  | 内部失败（native capability 视角） | POSIX errno |
  |----------------------------------|-------------|
  | 缺少对应 right / 越权 | `EPERM` |
  | 句柄不可见 / 未 reveal 路径 | `EACCES` / `ENOENT` |
  | 句柄类型错误 / 无效 handle | `EBADF` |
  | 对象已被 revoke / 取消 | `ECONNRESET`（对 socket）/ `EBADF`（对 fd） |
  | 资源配额耗尽（Job 账本） | `ENOMEM` / `EAGAIN` |
  | 协议不支持 / 子集外 | `ENOSYS` |
  | 参数布局非法 | `EINVAL` |

- **errno 存储**：建议每线程 `errno`（TLS，与 `21` §5.3 meuos-libc TLS 基座同构），避免全局锁争用；与 POSIX 兼容层语义对齐，且不污染 native 路径（native 程序可走「返回码 + 结构化错误」而非 errno）。
- **errno 翻译只在兼容边界做**：native capability 调用本身返回结构化错误（right 缺失原因等），薄层在返回 Linux 二进制前合成 errno（`08` §1.2 ④ 语义合成）。

### 3.2 用户可见结构体布局：冻结于 `23` ABI 稳定性纪律

> 继承 `23` §2 三层分级：**兼容子集（①）内的用户可见结构体布局 = 冻结项**，随 `.msys` 版本稳定，不随内核自由变更。

- **冻结集合**：兼容 ABI 声明子集内的结构体（`struct stat` / `struct timespec` / `struct utsname` / `struct sockaddr` 族 / `struct pollfd` 等）布局一旦声明，即进入 `23` ①「对外刚性」承诺——改动 = BC break，需弃用窗口（`23` §6.3，严于 IPC）。
- **不冻结**：native 层的结构体（capability handle 布局、Job manifest、IPC 消息 schema）随 `.msys` 演进（`23` ②），由 libc/mkit 屏蔽，不向外部二进制暴露。
- **与 `23` 协同**：结构体冻结清单由 `23` ① 稳定表面积界定，本文只声明「用户可见结构体布局属冻结项」，不替 `23` 选具体清单（§7 D7）。
- **五架构布局一致**：冻结结构体的字段序 / 对齐 / 大小在五架构上**必须一致**（同 C ABI 约定，如 `__attribute__((packed))` 或显式固定宽度），由 meuos-libc 统一保证（`21` §5.3 多架构落地），避免「同结构体五架构不同布局」破坏兼容子集。

---

## 4. syscall 版本化与 `.msys` 协同（版本协商、向后兼容窗口）

> 继承 `23` §5（`.msys` 同版本升级杠杆）+ §6（版本协商）+ §7（uname C14）。syscall 版本化**不绑定 uname 内部版本**，随 `.msys` 整卷演进。

### 4.1 两层版本命运（与 `23` §2 同构）

| 层 | syscall 子集 | 版本锚点 | 稳定承诺 |
|----|-------------|----------|----------|
| **① 兼容子集**（对外） | 统一编号中「声明为 Linux 兼容」的部分 | 绑定 `uname` 报告值（`23` §7 / C14）+ C12 覆盖基线 | **必须稳**，子集内改动 = BC break |
| **② native 子集**（对内演进） | 统一编号中「MeuOS native capability 调用」部分 | 随 `.msys` 版本走，**不**绑定 uname | **演进期明确不稳**，靠 libc/mkit 屏蔽 |

- **`.msys` 杠杆**（`23` §5）：在 `.msys` 边界内，内核 + libc + 在树服务同版本，native syscall 号 / capability 语义可自由演进——无需逐接口冻结。
- **统一编号的位用途**：建议用编号高位 bit 区分 ①/② 子集（如 `NATIVE_BIT`），使「同一张分发表」内原生与兼容请求可区分，而不必分两张表。

### 4.2 版本协商与向后兼容窗口

- **兼容子集（①）**：稳定承诺对外刚性，改动需弃用窗口（`23` §6.3，建议「声明弃用于 N 版、移除于 N+2 版」）；子集外一律 `ENOSYS`。
- **native 子集（②）**：随 `.msys` 原子升级；跨 `.msys` 未重编的原生二进制**不保证**可跑（`23` §5.3），由 libc 屏蔽层吸收破坏（`23` §2 ②）。
- **协商面**：native capability 调用的「版本」不依赖 syscall 号协商，而由 libc/mkit 与内核同 `.msys` 保证；用户态服务间 IPC 版本协商走 `05` `protocol_version`（`23` §6），syscall 层不直接做 IPC 式握手。
- **`uname` 角色**（`23` §7 / C14）：对外报 MeuOS 自主版本（`08` D7 已采纳），兼容层可经 manifest 提供「呈现值」供老程序探测；syscall 版本不由此推导。

---

## 5. native 路径（不经兼容层直接 capability 调用，与 `05`/`07` 衔接）

> **方案**：mkit 原生程序走「native 路径」——发 syscall（统一编号 `NATIVE_BIT` 置位）→ 内核薄层直接 dispatch 到 **native capability 调用**，不经过 Linux 语义翻译面。

### 5.1 native 路径与 compat 路径的同构终态

```
mkit 原生程序 发 syscall（NATIVE_BIT）
      │
      ▼
内核态 mkit 薄层（统一分发表，NATIVE 分支）
      │ 直接 dispatch
      ▼
native capability 调用（= 05/07 一等原语）
   · Channel / EventPort / SHM（05 §1）
   · VMO / VMAR（03 / 07）
   · Job / meu_spawn（04）
   · capability handle + rights 校验（07 §4.1，冻结位图）
```

- **同一终点**：native 路径与 compat 路径最终都落到 `05`/`07` 的 native capability 调用——区别只在「是否经过 Linux 语义翻译」。**native 路径省去翻译，是 lean hybrid 对宏内核零往返优势的继承**（`00` A10 / `08` §1.2）。
- **与 `05` 衔接**：native 程序的 IPC、VMO、Service Registry 操作直接以 capability handle 为参数，不经 fd 抽象；`05` 端点即 capability，cap-transfer 即委托（`00` A8）。
- **与 `07` 衔接**：native capability 调用直接触发 `07` rights 校验（句柄即 capability、权限随句柄走，A1）；无新增 right bit（冻结 bit 0–47，`07` §4.1）。

### 5.2 native 路径的「快」为何成立

- direct-mapped 子集的 mkit 程序，**native 路径与 compat 路径共用同一薄层分支**（都是 1:1 映射），仅差「是否把结果再翻成 errno/Linux 视图」——对 mkit 程序，这一步也可省略，直接返回原生错误结构。
- 因此 mkit 子集程序「零成本直跑」（`08` §2.3 Phase 0 自我验证基准）在 native 路径下成立；通用 glibc 程序经 compat 翻译，成本在语义仿真不在分发。

---

## 6. 与既有地基同构（capability Day1、`00` A1–A12、不引入新内核原语）

> 本节显式校验本文与已决地基的一致，证明「未引入新内核原语、未新增 right bit、五架构同源」。

### 6.1 与 `00` A1–A12 对齐

| 地基项 | 本文落点 |
|--------|----------|
| **A1** 句柄即 capability | syscall 终态 = capability 调用，权限随句柄走 |
| **A2** Day1 唯一安全模型 | 兼容层翻译 Linux 语义而非暴露特权，capability Day1 激活 |
| **A10** mkit 子集优先 + 内核态薄层 | 统一分发表 + direct-mapped 1:1 即 A10 具象 |
| **A11** `.msys` 自举/回滚媒介 | syscall 版本随 `.msys`（`§4`） |
| A6 尽快交权缩 TCB | emulated/synthesized 复杂语义退用户态（§2.3） |
| A3 Rust 管安全关键 | 薄层分发属内核 TCB 边界，按 `02` 入 TCB |

### 6.2 不引入新内核原语 / 不新增 right bit

- **无新内核原语**：syscall 入口不创造新的内核对象类型，仅 dispatch 到既有 `05`/`07`/`04`/`03` 原语（Channel/VMO/Job/capability）。兼容层是翻译面，不是新机制（`08` §5.6）。
- **无新 right bit**：兼容层把 Linux 能力（如 `CAP_NET_BIND_SERVICE`）映射到既有 `07` rights（`NET_BIND` 等，已冻结 bit 28–31/`NET_MANAGE`=39）；不新增 right（`07` §4.1 冻结纪律，`38`/`45` 统一表）。
- **跨域冲突裁决**：凡与 `05`/`07`/`04`/`06`/`08`/`11`/`21` 交叉项，以 `00` C1–C18 为准；本文只声明依赖、不替其下结论（如 C4 沙箱默认 deny、C12 syscall 覆盖基线、C14 uname）。

---

## 7. 待决项表（标注是否需大喵拍板）

| 编号 | 待决项 | 是否需大喵拍板 | 关联 |
|------|--------|----------------|------|
| **D1** | **统一 syscall 编号编码方案**：单一线性 32 位编号 vs 高位 bit 区分 native/compat 子集（本文推荐「单一线性 + 高位 NATIVE_BIT」）；编号保留位与扩展策略 | 否（设计协同，可在 doc-sync 定稿） | `08` §2.2、`21` §5.3、`23` §4 |
| **D2** | **兼容层是否进入 TCB**：direct-mapped 整形逻辑处内核态、默认入 TCB 边界，但 emulated/synthesized 复杂语义退用户态；「兼容层整体 TCB 归属」需定调 | **已裁决·闭环（第十轮自主采纳，分层 TCB）** | `00` A3/A6、`02` §5-6 TCB 度量、`08` §5.6 |
| **D3** | **errno 映射范围与存储**：内部 native 失败 → POSIX errno 精确映射表（§3.1）；每线程 TLS errno vs 全局；native 路径是否仍走 errno | 否（设计协同，协同 `07`/`21` libc 定稿） | `07` §4.1、`21` §5.3 TLS、`23` §3 |
| **D4** | **syscall 版本化策略**：native/compat 双子集如何分别版本；`.msys` 版本号是否直接作为 native syscall revision；兼容子集弃用窗口数值 | 否（设计协同，协同 `23` §5/§6/§7 定稿） | `23` §5/§6/§7、C12、C14、`06` §4.3 |
| **D5** | **native/compat 双路径长期维护成本**：双路径漂移风险、五架构 × 双路径测试矩阵、direct-mapped 子集变更如何同步两套语义 | **已裁决·闭环（第十轮自主采纳，direct-mapped 子集为权威 + CI 双路径门禁）** | `08` §2.2、`21` §5、`11` §5、`19` 性能/可验证性 |
| **D6** | **五架构入口蹦床归一约定**：per-arch 参数寄存器 → 内部 syscall frame 的整形 ABI；i386 纯 C 快通道与 Rust 接管边界（`21` §5.3 / C6） | 否（设计协同，协同 `21`/`09`/`11` 定稿） | `21` §4/§5.3、`09` §5.7、`11` §5 |
| **D7** | **用户可见结构体冻结清单**：哪些结构体进入 `23` ① 冻结集合（`stat`/`timespec`/`utsname`/`sockaddr`…）；五架构布局一致性强制手段 | 否（协同 `23` §2 定稿，本文只声明「属冻结项」） | `23` §2/§7、`21` §5.3 |
| **D8** | **direct-mapped 全集映射表初版**：统一编号中哪些 syscall 进 1:1 分支（mkit Phase0 子集），哪些进 emulated/synthesized | 否（实现阶段待决，协同 `08` §6.2 / C12） | `08` §2.2/§6.2、C12、`11` M0–M4 |

> 标注：本文为规划草案，后续以修订式追加更新，不覆盖、不删改 `00`–`70`、README、`.issues/`、git。跨域冲突以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准；本文未引入新内核原语或新 right bit，五架构 syscall 逻辑同源。待决项 D1/D3/D4/D6/D7/D8 为设计/执行层协同项，D2（兼容层 TCB 归属）与 D5（双路径长期维护成本）需大喵架构级拍板。

---

## 8. 参考文献（真实 URL，已核验可访问）

### 系统调用 ABI 参照（直接对标）
- **Linux syscall 表（x86_64，kernel `arch/x86/entry/syscalls/syscall_64.tbl`）**：https://github.com/torvalds/linux/blob/master/arch/x86/entry/syscalls/syscall_64.tbl
- **Linux syscalls 总览（man7）**：https://man7.org/linux/man-pages/man2/syscall.2.html
- **Linux `errno` 列表（errno(3)）**：https://man7.org/linux/man-pages/man3/errno.3.html
- **Fuchsia syscalls（zx_* 统一命名空间，vDSO 分发）**：https://fuchsia.dev/fuchsia-src/reference/syscalls
- **Zircon syscall 设计（系统调用公约 / handle/rights）**：https://fuchsia.dev/fuchsia-src/concepts/kernel/syscalls
- **seL4 invoke（capability 调用即系统调用，无 syscall 号表，靠 cap 调用方法）**：https://sel4.systems/Info/Docs/seL4-manual.pdf
- **Windows NT syscall 约定（int 0x2e / syscall 指令，编号随版本）**：https://en.wikipedia.org/wiki/Windows_NT

### ABI 稳定性 / 版本化参照
- **Fuchsia FIDL 版本化 / ABI revision**：https://fuchsia.dev/fuchsia-src/development/languages/fidl ；https://fuchsia.dev/fuchsia-src/concepts/system/abi-revision
- **Android Treble / vendor HAL 稳定接口**：https://source.android.com/docs/core/architecture/treble

### 项目内（已读，跨域对齐）
- `00-总览与路线图.md`（A1–A12 句柄即 capability、A10 内核态 mkit 薄层、A11 `.msys`、C1–C18 冲突登记）
- `05-IPC机制.md`（Channel/EventPort/SHM、端点即 capability、cap-transfer）
- `07-安全模型.md`（capability Day1、`rights` 位图冻结 bit 0–47 §4.1、受约束 mint §4.5、无新 right）
- `08-Linux兼容层.md`（薄翻译面 §1.2/§1.3、mkit 三类策略 §2.2、边界 §5.6、D1–D7 裁决）
- `11-自举验证方案.md`（M0–M4 syscall 子集、五架构自举验证 §5）
- `21-迁移路线图.md`（五架构代码生成、统一链接脚本、meuos-libc syscall gate §5.3）
- `23-ABI稳定性与版本.md`（三层分级 §2、`.msys` 杠杆 §5、版本协商 §6、uname C14）
- `63-网络栈实现深化.md`（mkit BSD socket 映射范式、device VMO 零拷贝、五架构仅 NIC 驱动差异）
- `06-文件系统.md`（`.msys` 原子更新 §4.3、每进程命名空间）
- `04-进程与调度.md`（Job 账本、meu_spawn、exception channel）

---

> 本文为规划草案，不修改 `00`–`70`、README、`.issues/`、git；结论供执行层与 `08`/`23`/`05`/`07`/`11`/`21` 引用。跨域冲突以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准。本文未引入新内核原语、未新增 right bit，五架构 syscall 逻辑同源；待决项 D1–D8 中 D2（兼容层 TCB 归属）与 D5（native/compat 双路径长期维护成本）需大喵架构级拍板，其余为设计/执行层协同项。

---

## 9. 第十轮自主采纳·闭环 D2 / D5

> 本节约等于第十轮自主采纳裁决，以修订式追加方式追加于文末，不改动 `00`–`70`、README、`.issues/`、git，亦不引入新内核原语或新 right bit；五架构 syscall 逻辑仍同源；跨域冲突仍以 `00` A1–A12 / C1–C18 为准。两项裁决由指挥官自主采纳，待大喵复核。

### 9.1 D2（兼容层是否进入 TCB）→ 采纳「分层 TCB」

**采纳结论**：兼容层不整体进入内核 TCB；仅内核态薄 direct-mapped 整形逻辑（参数规整、errno 映射表）按 `00` A3 入 TCB 边界并受 `02` TCB 度量，复杂 emulated/synthesized 语义（如 socketpair 模拟、signalfd 等）退用户态服务、不进 TCB。

**理由**：与 `00` A6「重服务出核、缩 TCB」一致，最小化 TCB 攻击面；与 `08` §5.6 薄层同源（兼容层是用户态蹦床、非内核 ABI 后门）。内核态薄层只做机械映射、不含安全策略，TCB 增量极小；emulated/synthesized 复杂语义降为可重启用户态组件，既保留兼容能力又不扩大内核信任边界。

**衔接**：
- 与 `00` A3（Rust 管安全关键）衔接——薄层分发属内核 TCB 边界，按 `02` §5-6 TCB 度量入册；
- 与 `00` A6 衔接——emulated/synthesized 退用户态，交权缩 TCB；
- 与 `08` §5.6 衔接——薄层四类职责中「权限裁决」仍在下游 native capability 调用处（`07` rights 校验），薄层只消费 capability 不发放；
- 与 §2.3「是否进入 TCB」红线一致——direct-mapped 整形默认入 TCB 边界、复杂语义不进，本裁决将其固化为定调。

### 9.2 D5（native/compat 双路径长期维护成本）→ 采纳「direct-mapped 子集为 single source of truth + CI 双路径一致性门禁」

**采纳结论**：(a) 将 direct-mapped 子集定义为权威（single source of truth），compat 路径视为其投影并自动生成测试；(b) 五架构 × 双路径矩阵以合成测试覆盖；(c) 长期收敛 native 优先。

**理由**：避免 native/compat 双路径语义漂移；与 `08` §2.2（mkit 三类策略：direct-mapped 为 1:1 分支）及 `21` §5（五架构代码生成 / meuos-libc syscall gate 同源）迁移路线一致。把 direct-mapped 子集作为唯一权威，compat 投影自动派生测试，从根上消除「同一语义两处维护」导致的漂移；CI 双路径一致性门禁保证五架构 × 双路径矩阵长期不退化。

**衔接**：
- 与 `08` §2.2 衔接——direct-mapped 子集是薄层 1:1 分支，本裁决将其升格为权威源；
- 与 `21` §5 衔接——meuos-libc syscall gate 面向统一编号发出 syscall，五架构 libc 同源，native 优先收敛路线复用该基座；
- 与 `11` §5 衔接——M0–M4 自举验证 syscall 子集为 unified 编号首批冻结项，CI 双路径门禁可挂接该验证矩阵；
- 与 §1.1 / §5 同源收口一致——分发逻辑、号→原语映射、errno 合成统一实现一次，五架构仅差入口蹦床，本裁决保证双路径投影长期同源于此单源。

> 待大喵复核。
