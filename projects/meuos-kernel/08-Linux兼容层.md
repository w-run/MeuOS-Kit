# 08-Linux 兼容层

> 文档定位：MeuOS 内核「百思」设计/规划阶段文档。
> 本文件只描述**从零设计的 Linux ABI 兼容层**之目标形态、边界、策略与张力拆解，**不含实现代码**。
> 状态：完整初稿（修订式追加，不删）。
> **本结论非最终，doc-sync 合成 pass 回填满跨域一致性**（与 01 内核总览、02 统一文件系统层、03 进程与 spawn、04 capability 安全、05 IPC、06 VM/内存、07 异常与信号 等文档对齐）。

---

## 0. 一句话结论（先给答案）

MeuOS 的 Linux 兼容层**不是把 Linux 内核搬进来**，而是「在 capability-based 自主内核之上，让 Linux 二进制误以为自己运行在 Linux 上」的一张**薄转换面**（ syscall → 内部 capability/IPC/VMO/Job 原语 → 合成 Linux 语义返回）。
它借鉴 FreeBSD linuxulator 的「薄层翻译」与 WSL1 的「用户态 syscall 转译」哲学，**不采用 WSL2 的「真·Linux 内核」路线，也不采用 Fuchsia 的「彻底不兼容」路线**。
优先让 **mkit 子集程序零成本直跑**，通用 glibc 程序按需补齐（fork 仿真为最大成本中心）；内部**默认 deny-by-default 的 pledge/unveil 沙箱不被 Linux 二进制自动绕过**，对外呈现的「uid 0」只是无额外权限的合成视图。

---

## 1. 「应该是什么样」——兼容层在内核的哪个边界

### 1.1 它不是什么

- 不是 Linux 内核源码的移植或 fork。
- 不是 Container / 命名空间（那些依然是真 Linux 内核）。
- 不是「再写一遍 Linux 语义」——那会动摇内部自主设计的统一性与安全模型。

### 1.2 它是什么：一张薄转换面

```
  Linux 二进制 (.text 直接发 syscall 指令)
        │  int 0x80 / syscall  # 原生 trap
        ▼
  ┌─────────────────────────────────────────────┐
  │  Linux 兼容层 (thin translation surface)      │
  │   ①  syscall 分发 (Linux syscall 号 → 内部分派) │
  │   ②  per-task Linux Context (fd 表/信号处置/    │
  │       pid 视图/uid 视图 —— 仅 Linux 侧状态)     │
  │   ③  意图翻译: Linux 语义 → 内部原语调用        │
  │   ④  语义合成: 内部结果 → Linux errno/返回值/    │
  │       信号投递 回到二进制                        │
  └─────────────────────────────────────────────┘
        │ 调用内部原语（不是 Linux 原语）
        ▼
  ┌─────────────────────────────────────────────┐
  │  MeuOS 自主内核内部 (Day1 唯一模型)            │
  │   capability handle │ VMO │ Job 资源账本       │
  │   统一命名空间 │ 异常 channel │ IPC channel     │
  │   pledge/unveil 沙箱 (默认 deny)              │
  └─────────────────────────────────────────────┘
```

### 1.3 薄层原则（铁律）

1. **权威只来自内部 capability**：兼容层本身不持有任何「root 逃生舱」。它只是把 Linux 二进制的请求翻译成对已有 capability 句柄的操作。二进制能做什么，由它所在 Job 的 capability 集合决定，与「它以为自己是 root」无关。
2. **Linux 侧状态最小化**：只保留 Linux 二进制「必须看见」的状态（fd 号、信号 disposition、pid/uid 视图、当前目录、umask）。这些状态是 *view*，不是 *authority*。
3. **能直连内部原语就不仿真**：能用一次内部原语表达的动作，不做等价 Linux 行为序列的复刻。
4. **翻译失败即 ENOSYS/EPERM 返回**，而非悄悄降级——宁可显式不兼容，也不制造「看起来能跑实则错」的缝合怪。

---

## 2. mkit 子集优先策略

### 2.1 什么是 mkit 子集

`mkit` 是 Kit 工具链自带的**精简 libc/ABI 子集**，是 MeuOS 内核**原生 ABI 的表面**（capability 友好、显式传参、无隐式全局状态）。
用 mkit 编译的程序，其系统调用入口**直接对应内部原语**（见 §2.2 的 direct-mapped 类别），几乎不经翻译面。

### 2.2 三类翻译策略（核心分类）

| 类别 | 适用对象 | 开销 | 做法 |
|------|----------|------|------|
| **direct-mapped** | mkit 子集程序 | 极低（接近原生） | Linux syscall 号与内部原语 1:1 对应，兼容层仅做参数整形与 errno 合成 |
| **emulated** | 通用 glibc 程序（fork/exec/信号等） | 中—高 | 将 Linux 复合语义翻译为：meu_spawn + Job 账本克隆 + 异常 channel 映射 |
| **synthesized** | 伪文件系统 /proc /sys /dev | 低（按需生成） | 由统一命名空间在查询时**生成 Linux 视图**，不落地真实文件 |

### 2.3 优先级顺序

1. **Phase 0 — mkit 子集直跑**：覆盖 mkit 使用的 syscall 集合（open/read/write/close、mmap、基本的进程/线程、基础信号）。目标：所有 Kit 自带工具与 mkit 程序**零成本运行**，这是兼容层的「自我验证基准」。
2. **Phase 1 — 静态/简单 glibc 程序**：无 fork、无复杂信号的 daemon/CLI（如多数 coreutils 形态程序）。
3. **Phase 2 — 动态 fork/exec 程序**：补齐 fork 仿真 + execve 翻译为 meu_spawn + 信号全集映射。
4. **Phase 3 — 重 IO / 伪文件系统依赖程序**：完整 /proc、/sys、/dev、futex、eventfd、timerfd、epoll 等。

> 为什么 mkit 优先：mkit ABI 是我们**自己设计的**，天然贴合内部 capability/Job/统一命名空间，因此直跑几乎无翻译成本；而通用 glibc 程序是把「Linux 的隐式契约」当成前提，必须逐条仿真。先赢下自己能掌控的子集，再啃最难啃的存量。

### 2.4 mkit 程序「直接映射」示例（意图级）

- `mkit_open(path, flags)` → 对统一命名空间做 capability 解析 → 拿到 VMO/文件 capability handle → 兼容层登记一个 Linux fd 号返回。
- `mkit_spawn(args, handles)` → 直接调用内部 `meu_spawn` + Job 账本，无 fork 语义。
- `mkit_mmap` → 直接分配 VMO 并映射，无 MAP_xxx 隐式行为差异。

---

## 3. 核心张力拆解：内部自主安全 vs 外部 Linux 契约

这是本文档要解决的中心矛盾。内部模型（Day1 唯一）：**无 root、句柄即 capability、pledge/unveil 默认 deny**。Linux 二进制却期望：**uid 0 万能、可直接 open 任意路径、完整信号、fork 免费**。

### 3.1 「root」是合成视图，不是权限

- 对外 `getuid()/geteuid()` 默认返回 **0**，`uname` 报告一个固定但可控的版本串（见 §6 D7）。
- 内部该「root」对应的 authority = 进程 Job 的 capability 集合。**uid 0 不授予任何超越 capability 集的权限**。
- 这与 Container 中「映射 root」同源，但更彻底：内部根本不存在 uid 提权路径，所以不存在「逃逸到真 root」。

### 3.2 「open 任意路径」由 unveil 视图决定

- Linux 二进制调用 `open("/etc/passwd", ...)` 时，兼容层把它翻译为对统一命名空间的 capability 查找。
- 若进程处于默认沙箱（pledge/unveil 收紧），只有**已 unveil 的路径前缀**能被解析成功；其余返回 `ENOENT`/`EACCES`。
- 二进制「以为」自己有个完整文件系统树，实际看到的是统一命名空间经 unveil 过滤后**投影出的 Linux 路径树**。

### 3.3 「fork 免费」是最大谎言 —— 成本中心

- 内部**没有 fork 语义**，只有显式 `meu_spawn`。Linux `fork()` 必须被仿真：
  - 方案 A（推荐起步）：基于 **VMO 写时复制 + 地址空间快照 + Job 账本克隆** 仿真 fork，子进程继承父进程 capability 视图的投影。成本高但语义完整。
  - 方案 B（性能优先）：仅对 mkit 静态子集提供「廉价 fork」，通用程序 fork 触发告警/降级。
- 设计取向：**meu_spawn 是原生快路径，fork 是兼容慢路径**。鼓励生态向 spawn 迁移，fork 仅为兼容存量。

### 3.4 「信号」翻译为异常 channel

- Linux 信号（SIGSEGV/SIGINT/SIGCHLD…）不对应内部真实异步中断模型。
- 兼容层把**子集信号**映射为内部**异常 channel** 投递：例如 SIGSEGV → 内存故障异常经 channel 回投为信号；SIGINT → 控制台中断事件转信号。
- 不支持的信号 / 实时信号子集 → `ENOSYS` 或合成默认处置，不假装全量支持。

### 3.5 兼容性逃生通道及其代价（关键权衡）

当某 Linux 程序确实需要「比默认沙箱更宽」的视图（如要读大量 /proc、要 bind 网络端口），提供**清单驱动的 broad-unveil / capability-grant 逃生通道**：

| 维度 | 保持默认沙箱 | 启用逃生通道 |
|------|--------------|--------------|
| 安全性 | 强（deny-by-default） | 弱（按清单放宽） |
| 兼容性 | 仅 mkit + 简单程序 | 覆盖多数 glibc 程序 |
| 审计 | 无需额外声明 | 需显式清单，可审计、可撤销 |
| 推荐 | 默认 | 仅限可信/本地程序，且清单纳入包元数据 |

> **代价核心**：放宽的不是「内核权限」，而是「该 Job 的 capability 投影范围」。即使逃生，也不存在「root 全能」——仍是 capability 集的显式扩大，可被 Job 账本追溯。

---

## 4. 与其它子系统的接口边界（仅声明依赖与协同点）

> 本节只画边界、列依赖与待协同点，不展开他域设计。

### 4.1 与统一文件系统层（02）
- **依赖**：统一命名空间必须能按路径解析出 capability handle，并支持「只读投影 / 前缀过滤」以供 unveil。
- **协同点**：`/proc`、`/sys`、`/dev` 由兼容层**向统一命名空间注册为生成式视图**；真实文件 IO 全部走统一层的校验和/压缩/加密与原子 `.msys` 更新，Linux 二进制无感。
- **待协同**：伪文件系统生成器的性能边界（见 §6 D5）。

### 4.2 与进程/meu_spawn（03）
- **依赖**：`meu_spawn` 必须暴露「克隆当前 Job 账本 + capability 视图」的入口，供 fork/execve 仿真复用。
- **协同点**：Linux `execve` = `meu_spawn` 的「替换镜像」模式；`fork` = spawn + VMO 快照（见 §3.3）。
- **待协同**：fork 快照的 VMO CoW 语义由 03 与 06 共同定义。

### 4.3 与 IPC（05）
- **依赖**：Linux `pipe`/`socket`/`futex`/`eventfd` 需映射到内部 IPC channel / 同步原语。
- **协同点**：futex → 内部原生同步；domain socket → IPC channel；共享内存 → VMO 投影。
- **待协同**：Linux `clone(CLONE_*` flags) 的细粒度共享语义如何映射到 Job/VMO 边界。

### 4.4 与 capability 安全（04）
- **依赖**：每个 Linux syscall 在执行前必须过一遍进程 capability 集的授权检查（与 pledge/unveil 同口径）。
- **协同点**：兼容层是 capability 的**消费方**，不是发放方。是否允许 Linux 二进制绕过默认沙箱是 §6 D1 的架构裁决。
- **待协同**：uid/gid 视图 ↔ capability 集的精确映射规则（§6 D6）。

### 4.5 与异常/信号（07）
- **依赖**：异常 channel 模型必须支持「向特定 task 投递可被兼容层翻译为信号的结构化事件」。
- **协同点**：见 §3.4。

---

## 5. 多系统参照与差异表（取长补短，不缝合）

| 系统 | 兼容路线 | 我们借鉴 | 我们不选 |
|------|----------|----------|----------|
| **Linux** | 原生 syscall 表 / binfmt / ptrace | syscall 分发表结构、ELF binfmt 加载、errno 约定 | 不搬内核、不做 ptrace 全量调试契约起步 |
| **Windows WSL1** | 用户态 syscall 转译（薄，无真 Linux 内核） | 「薄转换面」哲学、不跑真 Linux 内核也能跑 Linux 二进制 | 不绑定 NT 内核细节 |
| **Windows WSL2** | 真 Linux 内核跑在 VM | （无——与我们「内部自主」相悖） | 否决：等于承认内部不是自主内核 |
| **Windows WoW64** | 32→64 位翻译层 | 32/64 位 Linux 二进制分层的思路（§6 D3） | 不照搬 PE/TEB 机制 |
| **macOS XNU** | BSD 兼容层 via mach_syscall 翻译；无 Linux 层 | 「用原生 syscall 翻译旧 ABI」的层叠模式 | 不引入 mach 端口模型 |
| **Android** | bionic 替换 libc + ART 替换运行时；**非** glibc 二进制兼容 | 「提供自己的 ABI 子集（mkit）作为原生快路径」思路 | 不追求 glibc 二进制级兼容为第一目标 |
| **FreeBSD linuxulator** | 薄 syscall 翻译层 + per-process Linux ABI | **最贴近**：per-task Linux Context、薄层翻译、按需补齐 | 不照搬 FreeBSD syscall 号 |
| **Fuchsia** | 故意不兼容 Linux，靠 POSIX 层 (fdio) | 「POSIX 层是可选外套」的清醒认知 | 否决「彻底不兼容」：我们明确要跑 Linux 二进制 |

**结论性取舍**：采用「FreeBSD linuxulator + WSL1 薄层」的混合思路，mkit 作为 Android/bionic 式的原生子集，明确**拒绝 WSL2（放弃自主）与 Fuchsia（放弃兼容）两极**。

---

## 6. 待决清单与架构级裁决

> 下列项需在 doc-sync 合成 pass 或专项裁决会中定稿。

### 6.1 架构级裁决（需 team-lead / 大喵拍板）
- **D1 是否允许 Linux 二进制绕过 pledge 默认沙箱？**
  本文档推荐：**默认不允许**；仅通过显式清单（broad-unveil）按包粒度放宽，且记入 Job 账本。需裁决是否允许「全局开关」式放宽。
- **D2 fork 仿真程度**：全量 CoW（语义完整、成本高）vs 受限 fork（仅 mkit/静态）。推荐：mkit 直跑免 fork，通用程序起步即全量 CoW，后续优化。
- **D3 32 位 Linux 二进制（i386/arm）支持范围**：WoW64 式独立翻译层 vs 仅 64 位。涉及五架构后端投入。
- **D4 syscall 覆盖基线**：以哪个 glibc / 内核版本为「通用程序」目标？mkit 子集先行已定，glibc 基线待定。
- **D5 /proc /sys 伪文件系统生成策略与性能边界**：全量生成 vs 按需最小集；是否缓存。
- **D6 uid/gid 视图 ↔ 内部 capability 集的精确映射规则**：单 Job 单 uid 视图？多 uid 模拟（setuid 二进制）是否支持？
- **D7 对外 `uname` 报告的版本/ABI 号**：声称为何值以最大化存量程序兼容而不误导。

### 6.2 实现阶段待决（非架构）
- syscall 号与内部原语 1:1 映射表（direct-mapped 全集）的初版清单。
- per-task Linux Context 结构字段定义（fd 表上限、信号掩码、pending 信号队列）。
- ENOSYS 策略：直接返回 vs 记录到兼容性遥测。
- 与 02/03/04/05/06/07 的跨域一致性回填项（doc-sync 合成 pass）。

---

## 7. 非缝合怪声明（明确翻译 / 不翻译）

- **翻译**：mkit 子集 syscall、通用程序常见 syscall（IO/进程/基础信号/基础 IPC）、ELF 加载、errno、fd 语义、uid/gid 合成视图、/proc//sys//dev 生成视图。
- **仿真（高成本，按需）**：fork（VMO CoW）、execve→meu_spawn、信号→异常 channel。
- **不翻译 / 显式不兼容**：Linux 内核内部 ABI（如 `ioctl` 全量、`perf`、`bpf`、`netlink` 全量、`ptrace` 全功能）、依赖特定内核版本的深度耦合程序。这些返回 `ENOSYS`，**不制造假兼容**。
- **为何 mkit 优先**：mkit 是我们自己设计的原生 ABI 子集，零翻译成本且安全模型天然对齐；通用 glibc 是存量负担，按 Phase 0→3 渐进啃下，绝不为了「看起来兼容」而污染内部自主模型。

---

## 8. 自检清单（本文档主张的落点）

- [x] 兼容层定位为「薄转换面」，非 Linux 内核移植。
- [x] 薄层原则：权威只来自内部 capability，root 为合成视图。
- [x] mkit 子集优先 + 三类翻译策略（direct-mapped / emulated / synthesized）。
- [x] 核心张力拆解：root 视图、unveil 路径、fork 成本、信号→异常 channel、逃生通道代价。
- [x] 与 02/03/04/05/06/07 的接口边界与待协同点声明。
- [x] 多系统参照差异表 + 自主取舍结论。
- [x] 待决清单含架构级裁决（D1 沙箱绕过为首要裁决项）。
- [x] 标注「本结论非最终，doc-sync 合成 pass 回填满跨域一致性」。
