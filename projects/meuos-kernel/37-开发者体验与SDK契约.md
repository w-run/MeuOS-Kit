# 37 - 开发者体验与 SDK 契约（Developer Experience & SDK Contract）

> 子领域：把「内核规划」转成「可构建生态」的桥——开发者体验（DX）主线、SDK 对外契约、文档即契约方法论。
> 团队：`kernel-plan`；调研员：general-purpose-42（新范围拓展调研员，lite / hy3，指挥官模式独立士兵-agent）。
> 配套文档：`00-总览与路线图.md`、`02-内核语言策略.md`、`04-进程与调度.md`、`05-IPC机制.md`、`07-安全模型.md`、`08-Linux兼容层.md`、`12-调试与可观测性.md`、`21-迁移路线图.md`、`23-ABI稳定性与版本.md`。
>
> 方法论铁律（大喵，贯穿）：解构型思维（先推演「应该是什么样」）→ 多系统参照（Linux / Windows MSDN·Win32·WDK / macOS / Android NDK·SDK / 塞班 SDK / ChromeOS·Fuchsia FIDL / seL4）→ 非缝合怪（思想取长补短、实现自主）→ 修订式追加、不删。
>
> 本文为**规划草案**，结论可在后续查缺补漏轮次以**修订式追加**更新，不删。跨域项以总览 `C1`–`C16` 及 `31` 为准；凡涉及其他子文档的待决点，本文**只声明依赖、不替其下结论**。

---

## 0. 一句话结论（先给答案）

MeuOS 的开发者**主入口必须是 mkit 原生路线**（`meuos-libc` + 原生 syscall / capability API + `meu_spawn` / Job / Channel / VMO），Linux 兼容层（`08`）只是「让既有 Linux 二进制跑」的**副线**、不是开发主入口。MeuOS 应当把 **SDK 作为系统能力的「唯一对外出口」**，且 **SDK / 头文件 / 文档全部由机器可读的契约规格（类 Fuchsia FIDL 的 IDL）生成**——能力只经契约声明暴露，绝不靠「内核头文件即契约」的 Linux 式泄漏。这是把「自主内核 + mkit 母语 + Linux 兼容层 + 自研工具链」真正变成「可构建生态」的桥。

---

## 1. 「应该是什么样」——开发者体验与 SDK 契约推演

> 大喵铁律第一条：先问「在 MeuOS 上做开发**应该是什么样**」，而非先列功能。

### 1.1 解构 Linux 的开发者体验教训

Linux 的开发者体验是「**分裂且隐式泄漏内部**」的：

- **契约即头文件**：内核 UAPI 头（`/usr/include/asm-generic`、`linux/*.h`）是事实契约；syscall 号、结构体布局、ioctl 魔术字散落各头，ABI 稳定性靠「Linus 永不破坏用户空间」这一人治铁律兜底（见 `23` §1.1）。
- **文档与实现脱节**：man-pages 长期滞后于内核，且 man-pages 描述**用户态 syscall 语义**，对**内核内部 API 几乎无官方文档**（Linux 内核「internal API has no stable docs」是公认教训）；驱动开发者只能读源码。
- **二进制兼容靠「默认全稳」**：glibc 二进制耦合到内核 syscall 号，跨版本升级一旦破坏即灾难——这是 `23` §1.1 指出的「独立版本化逼出刚性稳定承诺」的根源。
- **调试是隐式特权**：`ptrace` 默认可 attach 同 uid 进程、`perf` 全局采样（见 `12` §3.2），与 capability 模型天然冲突。

**解构出的「不应该」**：① 契约不该是手写的 C 头；② 文档不该晚于、脱离实现；③ 能力不该靠「读内核头 + 直接发 syscall」泄漏到出树。

### 1.2 MeuOS 结构下「应该」的开发者主线

MeuOS 与 Linux 截然不同（综合 `00` §1、`08` §2、`23` §1.2）：

- **两套对外接口**（`23` §1.2）：① Linux 兼容 ABI（出树二进制，必须稳）；② 原生 syscall / capability API（在树，靠 libc/mkit 屏蔽）；③ 服务间 IPC（版本协商）。
- **mkit 是内核原生 ABI 的表面**（`08` §2.1）：`meuos-libc` 直接对应内部原语，零翻译成本，安全模型天然对齐。
- **`.msys` 同版本升级杠杆**（`23` §5）：内核 + libc + 监督服务 + 在树服务同 sysroot 一起升级，内部接口可明不稳。

由此推演「应该是什么样」：

| 维度 | 应该态 | 反例（缝合怪 / Linux 旧路） |
|------|--------|------------------------------|
| **开发主入口** | mkit 原生路线：`meuos-libc` + 原生 capability / `meu_spawn` / Channel / VMO / Job | 把 Linux 兼容层当成第一开发入口（污染内部模型、绕开 capability） |
| **能力暴露出口** | **SDK 唯一出口**：系统能力只经机器可读契约（IDL）声明并生成 SDK/头/文档 | 开发者直接 `#include` 内核内部头、直接发裸 syscall（Linux 式泄漏） |
| **契约形态** | 机器可读 IDL（capability/rights、IPC 协议、Job manifest、设备能力） **单一真相源**，生成头/绑定/文档 | 「内核 C 头即契约」+ 散落 man-page |
| **稳定性承诺** | 三层分级映射到 SDK 版本（`23` §2）：① 兼容 ABI 稳、② 原生 API 不稳靠 libc 屏蔽、③ IPC 版本协商 | 一刀切「全稳」（锁死演进）或「全不稳」（生态碎） |
| **跨架构一致** | mcc 统一目标 + 单一 build 图，五架构产出一致 SDK | 每架构各写一套 SDK / 头（加剧五架构一致性成本，`00` §5.1） |
| **调试体验** | 经 capability 守护（`DBG_*`）+ inspect 树 + 事件流，默认 deny（`12` §4.2） | 默认可 ptrace 同 uid、perf 全局 |

### 1.3 核心命题：SDK 唯一出口原则（Single-Export-Gate）

> **系统能力只经 SDK（由契约 IDL 生成）对外暴露；内部类型、内部 syscall 号、内部 rights 位布局、内部结构体内存布局，禁止在 SDK 之外以任何形式「文档化泄漏」**。

这把 `07`「无 root、句柄即 capability、无隐式权限」延伸到**文档/API 层面**：文档也必须是 capability 受限的——开发者能看到的，只能是「被授权的 SDK 表面」，而非内核内部全貌。这与 Fuchsia「系统 API 经 SDK 唯一出口、FIDL 即契约文档」同构（见 §2）。

### 1.4 文档即契约（Docs-as-Contract）：为什么必须机器可读

`07` 定义了 capability / rights 位图（基础 7 + `KEY_*` 2 + `DBG_*` 6 + `CFG_*` 3，见 `07` §第四轮裁决回写）、`05` 定义了 IPC 协议、`04` 定义了 Job manifest。这些东西若以**散文 + 手填 C 头**维护，必然出现：

- rights 位图改了但头没改 → 出树 SDK 静默错配；
- IPC 协议字段增删但文档滞后 → 服务间协商失败（`23` §6）；
- Job manifest 字段与实现不一致 → spawn 行为漂移。

**解构结论**：这些规格应当有**机器可读单一真相源**，由它生成三样东西——① C/ Rust 头与绑定（SDK）、② 人类可读文档、③ 版本/协商元数据（`protocol_version` / `min_compatible_version`，`23` §6.1）。单一真相源改一处，三样同步，根除「内核头即契约」的 Linux 教训。

---

## 2. 多系统参照对比表（含「文档即契约」列）

> 大喵铁律第二条：多系统参照，思想取长补短，实现自主。下列系统覆盖任务指定的全部参照系。

| 系统 | 开发者体验主线 | SDK / 文档契约形态 | **文档即契约？** | 对 MeuOS 的取舍 |
|------|----------------|--------------------|------------------|------------------|
| **Linux** | glibc + 直接 syscall + man-pages | 内核 UAPI 头即契约；man-pages 滞后 | **否**——头与文档分离、内部 API 无官方文档 | **不借**「头即契约」；借「stable syscall 承诺」思想但仅限 `23` ① 兼容子集 |
| **Windows** | Win32 / MSDN / WDK | MSDN 强文档 + WDK（驱动 SDK 版本化，WDF） | 半——MSDN 是权威文档，但非机器生成 | 借「强文档 + 驱动 SDK 版本化（WDF）」思想（`23` §4.2）；不借 COM/私有 IPC |
| **macOS** | Xcode / Swift·ObjC 生态 / libSystem | 强 libSystem + 严格文档；DriverKit 用户态化 | 半——文档强，但非 IDL 生成 | 借「libSystem 屏蔽原生不稳定 syscall」（`23` §3 macOS 行）；不借 mach 端口模型 |
| **Android** | NDK / SDK 版本化 + Codelabs + 开发者指南 | NDK/SDK 明确版本号 + Treble vendor HAL 稳定接口 | 半——SDK 版本化强，但 HAL 非 IDL 单源 | 借「SDK 版本化 + 只在必要边界画稳定接口（Treble）」→ 对应 `23` ①③ 边界化 |
| **塞班** | SDK（严格 BC break 管理委员会） | SDK 强管控、全局 BC 流程 | 半——SDK 管控严但流程过重 | **不借**全局 BC 委员会；以 `.msys` 同版本升级替代其职能（`23` §1.3） |
| **ChromeOS** | 受控 Web/Android 应用生态 | 应用走 Web/Android SDK，系统能力经框架出口 | 是（框架即出口） | 借「系统能力经统一框架出口」精神，但 MeuOS 用 IDL 而非 Web 框架 |
| **Fuchsia** | **FIDL 即契约文档**；SDK 由 FIDL 生成；系统 API 经 SDK 唯一出口 | **FIDL（IDL）单一真相源 → 生成 SDK/头/文档/bindings**；文档即代码 | **是（最贴切）** | **主参考**：IDL 单一真相源 + SDK 唯一出口 + 原生 API 不承诺稳（`23` §3 Fuchsia 行） |
| **seL4** | sel4tools + 精炼文档；**证明即规格** | 文档与证明同源；API 小且精 | 是（证明即规格，规格即文档） | 借「规格即文档、接口小而边界清」；不借微内核范式 |
| **MeuOS（本文）** | **mkit 原生主线**；Linux 兼容层副线 | **IDL 单一真相源 → 生成 SDK/头/文档/版本元数据**；SDK 唯一出口 | **是（采纳 Fuchsia 范式 + seL4 规格同源）** | 三层分级映射 SDK 版本（`23`）+ mcc 跨五架构统一产出 |

**结论骨架**：
- **Fuchsia 的「FIDL 即契约文档 + SDK 由 FIDL 生成 + 系统 API 经 SDK 唯一出口 + 原生 API 不承诺稳」** 是本文的**直接范本**（对应 `23` ② 不稳 + ③ 版本化 + 本文 SDK 唯一出口）。
- **seL4 的「证明即规格、规格即文档、接口小而边界清」** 强化了「契约机器可读、单一真相源」的纪律（对应 `02` §4.4 最小 TCB + `07` 形式化可选路径）。
- **Android Treble 的「只在必要边界画稳定接口」** 对应 `23` 把稳定承诺只落在 ① 兼容 ABI 与 ③ IPC 协议两道边界。
- **Windows WDF / macOS DriverKit 的「版本化驱动 SDK」** 对应 `23` §4 用户态驱动经版本化 IPC 可版本化。
- **不借**：Linux「头即契约 + 内部无文档」、塞班「全局 BC 委员会」、任何「文档晚于实现」的模式。实现全部自主研究。

---

## 3. MeuOS 开发者主线（mkit）+ SDK 唯一出口模型

### 3.1 开发者主线＝mkit 原生，兼容层＝副线

与 `08` §2.3 完全一致，但把「开发入口」语义显式化：

1. **Phase 0 开发者：mkit 原生程序**（主入口）。开发者用 `meuos-libc` + 原生 capability API（`meu_spawn` / Channel / VMO / Job / handle+rights）编写程序。**这是 MeuOS 的「母语」开发路径**，零翻译、安全模型天然对齐、跨五架构一致。
2. **Phase 1–3 副线：让既有 Linux 程序跑**。通用 glibc 程序经 `08` 兼容层翻译运行——开发者**不应**把兼容层当成新程序的首选开发入口；它是「存量兼容」通道，不是生态构建通道。
3. **能力边界清晰**：mkit 程序直接持 capability 句柄；glibc 程序的能力是兼容层翻译出来的投影（见 `08` §3）。开发者要「用好 MeuOS 能力」，必须走 mkit 主线。

> 与 `08` 的衔接：本文不修改 `08` 任何结论，仅把 `08` 已定的「mkit 子集优先」升级为「**mkit 是开发主入口、兼容层是运行副线**」的 DX 表述。

### 3.2 SDK 唯一出口：IDL 单一真相源 → 三产物

定义 MeuOS 的 **IDL（接口描述语言，暂称 `meu-idldef`）** 作为机器可读单一真相源，覆盖四类规格（分别对 `07`/`05`/`04`/`10`）：

| IDL 规格类别 | 描述对象 | 生成产物 | 对应子文档 |
|--------------|----------|----------|------------|
| **capability / rights 规格** | rights 位图（基础 7 + `KEY_*` + `DBG_*` + `CFG_*`）、对象类型→rights 约束、mint 上限 | C/ Rust rights 常量头 + SDK + 文档 | `07`（§第四轮裁决回写） |
| **IPC 协议规格** | Channel 消息 schema、type tag、服务协议 `protocol_version`/`min_compatible_version` | stub/proxy 绑定 + 文档 + 协商元数据 | `05`（§4.1）、`23`（§6） |
| **Job manifest 规格** | spawn 输入清单：资源配额 / 初始 capability 集 / namespace 模板 / `debug_policy` | manifest schema + 校验器 + 文档 | `04`（§4.4）、`07`（§4.3 C3） |
| **设备能力规格** | 设备能力（MMIO/IRQ/DMA/IO port）schema、驱动协议 | 驱动 SDK 绑定 + 文档 | `10`（§4.6）、`23`（§4.2） |

**单一真相源改一处，三产物同步生成**：
```
meu-idldef (capability.fidl / ipc/*.fidl / job-manifest.fidl / device.fidl)
      │
      ├─► meu-codegen → C 头 (meuos-libc SDK) + Rust bindings
      ├─► meu-docgen  → 人类可读文档 (SDK 参考，含 rights/协议/manifest 字段)
      └─► meu-vergen  → 版本元数据 (protocol_version / min_compatible_version / ABI revision)
```

### 3.3 SDK 唯一出口原则的执行纪律

1. **内部不泄漏**：内核内部类型、裸 syscall 号、rights 位物理布局、结构体内存布局，**不进 SDK、不进公开文档**。开发者只看到 IDL 生成的 SDK 表面（对齐 `07`「无隐式权限」延伸到文档层）。
2. **原生 API 不稳靠 libc 屏蔽**（`23` ②）：mkit / `meuos-libc` 是 SDK 与内部原语间的唯一屏蔽层；原生程序**经 SDK 而非裸 syscall**（违反者不被 SDK 支持、跨 `.msys` 不保证，见 `23` §2）。
3. **出树第三方服务的契约边界**（`23` §8 待决 3）：出树第三方服务要么经 libc（② 屏蔽）要么经 IPC（③ 版本协商），**禁止直接发裸原生 syscall**，否则失去 `.msys` 同版本保护——这是 SDK 唯一出口对「出树」的硬约束。
4. **调试体验纳入 SDK**：`DBG_*` 调试权（`12` §4.2 / `07` §C9）作为 SDK 暴露的 capability，inspect 树 / 事件流订阅经 capability 守护，默认 deny（见 §4.4 与 `12` 接口）。

### 3.4 跨五架构开发一致性（mcc 统一目标）

依据 `21` §1.1 / `02` §2.2：mcc 已有 x86_64 / aarch64 / riscv64 / i386 / loongarch64 + arm 的完整代码生成。SDK 唯一出口在此的落地：

- **单一 IDL + 单一 `meu-codegen`，五架构产出一致 SDK 头**：开发者写一份 mkit 程序，五架构用同一 SDK 表面，杜绝「每架构各写一套头」加剧 `00` §5.1 的「五架构一致性成本」。
- **i386 例外的一致性**：i386 用户态 32 位按需求评估、不承诺全量 glibc 32 位（`08` C11 / `02` C2），但其 mkit 子集 SDK 仍由同一 IDL 生成，与 64 位同表面（仅范围不同）。
- **sysroot 双重身份支撑 SDK**：`.msys` 既是编译时 sysroot（mcc 取头）又是运行期 rootfs（`21` §1.2 咬合点 B），SDK 头与运行库来自同一 `.msys`，避免编译/运行两体分裂。

---

## 4. 与 02 / 08 / 12 / 21 / 23 的接口

> 本节只画边界、列依赖与待协同点，不展开他域设计（遵循硬约束：不修改 00–31）。

### 4.1 与 `02-内核语言策略`（工具链/FFI）
- **依赖**：`02` §4.3 的 FFI 边界（Rust 侧 `extern "C"`、C 侧 bindgen、abstractions 层）是 IDL 生成 Rust/C 绑定的落点——`meu-codegen` 产出的 Rust bindings 应经 abstractions 层接入，不裸持 C 指针（对齐 `02` §4.4）。
- **协同点**：IDL 生成的头文件随 `.msys` sysroot 分发（`21` §3.2），mcc 编译 mkit 程序时取用；Rust 部分（TCB）的 capability 类型由 IDL 生成、与 `02` §4.4「capability 不可伪造由 Rust 类型强制」互补。
- **待协同**：IDL 生成的 Rust binding 是否纳入 `02` 的 `unsafe` 审计清单（`02` §4.4）；双工具链（C1）下 IDL 工具自身由哪条工具链构建。

### 4.2 与 `08-Linux兼容层`（Linux 程序走副线）
- **依赖**：`08` §2.2 direct-mapped / emulated / synthesized 三类翻译策略是兼容层内部契约；**开发者体验上，兼容层是副线**——本文不替 `08` 定兼容语义，仅确认「mkit 主线 + 兼容层副线」的 DX 分层。
- **协同点**：兼容层对 Linux 程序**不暴露 SDK 原生能力**（避免「走兼容层却拿到 mkit capability」的越界）；Linux 开发者要调用 MeuOS 原生能力，须改写为 mkit 程序（走 SDK 主线）。
- **待协同**：兼容层 `uname` 对外稳定串（`08` C14 / `23` §7）是 ① 兼容 ABI 的对外锚点，本文 SDK 版本体系（`23` ③ 独立于 uname）不与之混用。

### 4.3 与 `12-调试与可观测性`（gdb/strace 体验）
- **依赖**：`12` §4.2 的 `DBG_*` rights（6 个）+ §4.6 的「strace→事件订阅、ptrace→`DBG_ATTACH`、perf→`DBG_PROFILE`、/proc→inspect 子树」是调试 SDK 的契约来源。
- **SDK 接口**：调试权经 IDL 生成为 capability SDK；开发者用 `DBG_*` 句柄经 Channel 订阅事件流 / attach，默认 deny（mkit 与 glibc 完全一致，`12` §R3-5）。
- **原型期桥接**：`12` §R3-6 的 QEMU gdbstub 兜底（VM 边界调试权不进 capability 模型）是 bring-up 桥接，SDK 文档应明确标注「生产 in-guest 调试回到 capability 守护」，不污染内部模型。

### 4.4 与 `21-迁移路线图`（Kit 编译/链接产出 SDK）
- **依赖**：`21` §1.2 三咬合点（编译边界 A / sysroot 双重身份 B / bootstrap 边界 C）是 SDK 分发载体——`meu-idldef` 工具与 `meu-codegen`/`meu-docgen`/`meu-vergen` 应纳入 Kit 工具链，随 `.msys` sysroot 一并产出与分发。
- **协同点**：bootstrap.sh Phase K（`21` §1.2 咬合点 C）除产出内核镜像，还应产出**匹配该 `.msys` 版本的 SDK 包**（头 + 绑定 + 文档 + 版本元数据），实现「内核 + libc + 服务 + SDK 同版本整体交付」——这是 `.msys` 同版本升级杠杆（`23` §5）在 DX 维度的延伸。
- **待协同**：`21` §4.2 待补项（mcc freestanding、mt/ld 内核链接脚本、libmsys 移植）是否也需产 IDL 工具的 freestanding 变体；IDL 工具本身的自举（吃自己狗粮）路径。

### 4.5 与 `23-ABI稳定性与版本`（稳定性分级映射到 SDK 版本）
- **依赖**：`23` §2 三层分级是 SDK 版本策略的权威依据——SDK 对外承诺必须映射到三层：
  - **① Linux 兼容 ABI**：SDK 暴露的兼容 syscall 子集**必须稳**（仅 `08` 声明子集），版本锚定 `uname` 串（`23` §7）。
  - **② 原生 syscall / capability API**：SDK 此部分**演进期明确不稳**，靠 mkit/libc 屏蔽；SDK 版本随 `.msys` 走，**不**绑定 uname。
  - **③ 服务间 IPC**：SDK 暴露的 IPC 协议**版本协商式稳**，自带 `protocol_version`/`min_compatible_version`（`23` §6.1），由 IDL 生成版本元数据。
- **协同点**：SDK 版本号体系与 `23` ③ 的 IPC 版本体系同源（`meu-vergen` 产出），独立于 `uname` 与 `.msys` 版本，避免「从 uname 误读内部稳定性」（`23` §7）。
- **待协同**：`23` §8 待决 1（② 是否存在亚稳子面，如 rights 位语义 / Job 账本字段是否比裸 syscall 更稳）——本文主张 **rights 位图与 Job manifest 经 IDL 生成，应作为「亚稳契约」受更严弃用窗口**（见 §5 待决）。

---

## 5. 自主结论 + 待决项

### 5.1 自主结论（非缝合怪总结）

1. **开发者主线＝mkit 原生，兼容层＝副线**：MeuOS 生态构建走 mkit（`meuos-libc` + 原生 capability / `meu_spawn` / Channel / VMO / Job），Linux 兼容层只负责跑既有二进制，不是开发入口。
2. **SDK 唯一出口 + 文档即契约**：系统能力只经机器可读 IDL（类 Fuchsia FIDL）生成的 SDK 暴露；capability/rights（`07`）、IPC 协议（`05`）、Job manifest（`04`）、设备能力（`10`）单一真相源生成 SDK/头/文档/版本元数据，根除「内核头即契约」的 Linux 教训。
3. **三层稳定性映射到 SDK 版本**（`23`）：① 兼容 ABI 稳（锚 uname）、② 原生 API 不稳（靠 libc 屏蔽）、③ IPC 版本协商（自带版本号），由 `meu-vergen` 统一产出。
4. **跨五架构一致**：mcc 统一目标 + 单一 IDL/codegen，五架构产出一致 SDK 表面，抑制 `00` §5.1 的五架构一致性成本。
5. **调试即 capability 纳入 SDK**（`12`）：`DBG_*` 经 IDL 生成、默认 deny、经 Channel 守护；原型期 QEMU gdbstub 作桥接不污染模型。
6. **文档是 capability 受限的**：SDK 唯一出口把 `07`「无隐式权限」延伸到文档层——开发者只见被授权的 SDK 表面，不见内核内部。

### 5.2 待决清单（标注是否需架构裁决）

> 凡需 team-lead / 大喵拍板者标「**架构级**」，其余为设计/实测协同项。

1. **【架构级·核心】是否强制「所有系统能力经 IDL 生成 SDK、禁止其他出口」？**
   本文核心提案：SDK 唯一出口为硬纪律。但存在张力——内核内部调试/早期启动是否允许临时直连？建议：**对出树开发者强制唯一出口；内核内部（TCB/Rust）可直连内部原语，但其类型仍由 IDL 生成以保证单一真相源**。需 team-lead 裁决「唯一出口」的边界（是否允许内部代码绕过 IDL 直接写结构体内存布局）。
2. **【架构级】IDL 是否覆盖「亚稳契约」（rights 位图 / Job 账本字段）？**
   `23` §8 待决 1 问②是否存在亚稳子面。本文主张：rights 位图与 Job manifest 经 IDL 生成后，应比裸 syscall 更稳（受更严弃用窗口），因其被 SDK 大量消费且跨 `.msys` 调试/审计工具（`12`）依赖。需 `07`/`04`/`23` 协同裁决亚稳边界。
3. **【架构级】IPC 协议版本号归属**（`23` §8 待决 2）：`protocol_version` 是每服务自管还是系统统一分配？本文主张 IDL 工具提供「平台保留 IDL 命名空间 + 服务私有 IDL 命名空间」双轨（仿 Fuchsia 平台 FIDL vs 服务私有 FIDL），需 `05` 协同裁决。
4. **【设计协同】IDL 工具链归属 Kit 还是内核仓库**：`meu-idldef`/`meu-codegen` 应纳入 Kit 工具链（`21` §1.1）随 `.msys` 分发，还是独立仓库？建议归入 Kit（与 mcc/mt 同级），需 `21` 协同。
5. **【设计协同】SDK 包与 `.msys` 版本绑定粒度**：bootstrap Phase K 产出的 SDK 包是否每 `.msys` 版本一份、随 sysroot 原子切换（`23` §5 杠杆）？需 `21`/`06` 协同。
6. **【设计协同】用户态驱动协议版本是否并入 ③ 体系**（`23` §4.3 / §8 待决 4）：设备能力 schema 经 IDL 生成后，是否复用 ③ `protocol_version` 还是独立编号？需 `10`/`05` 协同。
7. **【实测】IDL codegen 开销与五架构一致性验证**：codegen 在五架构产出头的一致性如何保障（同一 IDL → 五架构 ABI 一致）？需原型基准（关联 `00` §5.1）。

### 5.3 与总览/其他子文档的衔接声明

- 本文是 `00` 未覆盖的**新维度**（开发者体验 + SDK 契约视角），不修改 `00`–`31`、README、git、`.issues/`。
- 跨域冲突以总览 `C1`–`C16` 及 `31` 为准；本文待决 1/2/3 为架构级，建议汇入总览冲突登记表（参照 C1–C14 机制），由 team-lead 在聚合轮次拍板。
- 多系统参照结论（主参考 Fuchsia FIDL 即契约 + seL4 规格同源 + Android Treble 边界化 + NT WDF / macOS DriverKit 版本化驱动）供 `00` 后续修订式追加参考。
- 与 `08` 的衔接：本文把 `08` 已定的「mkit 子集优先」升级为 DX 表述，不改动 `08` 任何结论。

---

> 文档状态：规划草案 v1（新范围拓展调研员产出，lite / hy3）。采用**修订式追加、不删**；跨域项以 `00-总览与路线图.md` 的 `C1`–`C16` 及 `31` 为准。
