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

---

## 第五轮裁决回写（37-待决1/2/3）

> 裁决者：MeuOS 内核规划调研员（lite / hy3，自主采纳，无需大喵拍板）。
> 依据：`38` §4（37-待决1/2/3 建议立场）、本文 §1–§5、`23`（三层稳定性分级 + IPC 版本协商）、`36`（静态 TCB + 用户态组件化热升级）、`07`（capability / rights 位图）。
> 方法论铁律：解构型思维 → 多系统参照（Linux UAPI 稳定性规则 / Windows Win32·WinRT 契约 / Android NDK·NDK ABI / macOS SDK 契约 / Fuchsia FIDL 唯一 IDL 出口）→ 非缝合怪 → 修订式追加不删。
> 性质：**修订式追加**，不删既有结论。

### 0. 一句话裁决（先给结论）

三项待决**全部自主采纳**，采纳立场与 `38` 建议完全一致，并进一步收口为可执行纪律：

1. **37-待决1（SDK 唯一出口）— 采纳**：强制「所有出树系统能力只经 `meu-idldef` 生成的 SDK 暴露、禁止任何其他出口」；内核内部（TCB / Rust）可直连内部原语，但其类型仍由 IDL 生成以保证单一真相源。
2. **37-待决2（亚稳契约纳入 IDL）— 采纳**：rights 位图与 Job manifest 纳入 IDL 单一真相源，作为「亚稳契约」受比裸 syscall（②）更严的弃用窗口——因其被 SDK / 调试（`12`）/ 审计工具跨 `.msys` 版本大量消费。
3. **37-待决3（IPC 版本号双轨）— 采纳**：平台保留 IDL 命名空间 + 服务私有 IDL 命名空间双轨（仿 Fuchsia 平台 FIDL vs 服务私有 FIDL）；平台命名空间协议版本由系统统一分配，私有命名空间由服务自管但须在 Service Registry 声明 `protocol_version` / `min_compatible_version`。

**对任务聚焦三问的明确结论**：
- **是否承诺稳定 syscall ABI？** —— **否（对原生 syscall 不承诺稳定）**。稳定承诺只落在 `23` 三层里的 ① Linux 兼容 ABI（仅声明子集）与 ③ IPC 协议（版本协商）。原生 syscall / capability API（②）演进期明确不稳、保留破坏权，靠 `meuos-libc` / `mkit` 屏蔽。即 MeuOS 的「稳定面」是 **SDK / IDL 生成物 + 兼容层 + 版本化 IPC**，而非裸 syscall 号——与 Fuchsia「明确不承诺 syscall 稳定」完全同构，比 Windows Win32 / WinRT 的「用户态 ABI 刚性稳定」更克制。
- **版本化机制？** —— 三层映射：① 兼容 ABI 锚 `uname`（`23` §7）；② 原生 API 随 `.msys` 走、不绑定 uname；③ IPC 协议走 `protocol_version`/`min_compatible_version` 协商（`23` §6）+ 亚稳契约受更严弃用窗口（本裁决 待决2）。弃用窗口与 `.msys` 发布节奏绑定（声明于 N、移除于 N+2，`23` §6.3）。
- **是否单一 IDL / 头文件生成源？** —— **是**。`meu-idldef` 为唯一机器可读契约真相源，经 `meu-codegen` / `meu-docgen` / `meu-vergen` 生成 C / Rust 头、文档、版本元数据，根除「内核头即契约」的 Linux 教训。

### 1. 37-待决1：SDK 唯一出口边界 — 采纳

**裁决结论**：强制「SDK 唯一出口（Single-Export-Gate）」为硬纪律，边界划定为：

- **出树开发者（第三方 app / 出树服务）**：所有系统能力只经 `meu-idldef` 生成的 SDK 暴露。**禁止** ① 直接 `#include` 内核内部头；② 直接发裸原生 syscall；③ 自行 memcpy 内核结构体内存布局。违反者不被 SDK 支持、跨 `.msys` 版本不保证（`23` §2 ②）。
- **内核内部（TCB / Rust 实现）**：允许直连内部原语以提升性能与可验证性（对齐 `02` §4.4 能力类型由 Rust 强制、seL4 静态心态）；但**其类型仍由 IDL 生成**（如 capability / rights 枚举、Job manifest 结构），保证「单一真相源」——内部实现可读 IDL 生成的同一份类型定义，而非手写第二份。即「内部可绕过 SDK 调用路径，但不可绕过 IDL 类型真相源」。
- **Linux 兼容层（`08`）**：作为副线，其对外稳定面由 ① 兼容 ABI 承载，不暴露原生 SDK 能力（`37` §4.2）。

**理由（解构 + 多系统参照）**：
- **Linux 教训**：UAPI 头散落、ABI 稳定性靠「Linus 铁律」人治兜底、内部 API 无官方文档——这是「契约即 C 头」的失败范式。MeuOS 用 IDL 单一真相源 + 三产物同步根除该教训（`37` §1.1 / §1.3）。
- **Fuchsia 范本**：FIDL 即契约文档、系统 API 经 SDK 唯一出口、原生 API 不承诺稳（`37` §2 / `23` §3）——直接同构，采纳其边界。
- **Windows Win32 / WinRT 契约**：Win32 用户态 ABI 强稳定 + WDK 驱动 SDK 版本化（`23` §3）——借其「强文档 + 版本化 SDK」思想，但不借其「用户态 syscall 刚性稳定」包袱（MeuOS 原生 API 不稳，靠 libc 屏蔽）。
- **macOS SDK 契约**：libSystem 屏蔽原生不稳 syscall（`23` §3）——印证「原生不稳 + lib 屏蔽」路线。
- **与 `36` 一致性**：静态 TCB（`36` Tier A）不运行期加载，SDK 表面在构建期由 IDL 固定生成并随 `.msys` 分发，运行期无「动态扩面」需求——唯一出口纪律与「TCB 静态」天然咬合。

### 2. 37-待决2：亚稳契约（rights 位图 / Job manifest）纳入 IDL — 采纳

**裁决结论**：rights 位图与 Job manifest 字段**纳入 `meu-idldef` 单一真相源生成**，并定为「亚稳契约」：
- **亚稳定义**：比裸 syscall（② 原生 API）更稳，但比 ① 兼容 ABI 弱。即原生单点 syscall 可随时改号 / 改语义（靠 libc 屏蔽），但 rights 位语义、Job 账本字段这类「被全平台工具横向消费」的契约，变更须走**版本号 + 更严弃用窗口**。
- **弃用窗口**：亚稳契约变更窗口严于 ②（原生 API 可随时破），建议对齐 ① 的严度方向但不必等同 `uname` 级——具体值由 `07` / `04` 在 `meu-vergen` 输出里声明 `rights_revision` / `job_manifest_revision`（`23` §6.3 同构，与 `.msys` 发布节奏绑定）。
- **版本元数据**：`meu-vergen` 产出 `rights_revision` / `job_manifest_revision`，调试器（`12`）、审计工具、跨 `.msys` 迁移工具据此对齐，杜绝「rights 位改了头没改」的静默错配（`37` §1.4）。

**理由（解构 + 多系统参照）**：
- **seL4「规格即文档、证明即规格」**：接口小而边界清、规格机器可读——亚稳契约经 IDL 生成即「规格同源」，与 seL4 纪律同构（`37` §2）。
- **Android NDK ABI / Treble**：只在必要边界画稳定接口——亚稳契约正是「必要边界」的子集（rights / Job manifest 是 capability 模型骨架，必稳），与 Treble 边界化思想一致（`23` §3）。
- **与 `23` §8 待决1 协同**：本裁决正面回答 `23` 待决1——原生 API（②）确有亚稳子面，即 rights 位语义与 Job 账本字段；二者经 IDL 生成后受更严弃用窗口，既保住「内部柔性」杠杆（`23` §5），又给跨 `.msys` 调试 / 审计工具（`12`）一个可对齐锚点。
- **与 `36` 一致性**：热升级滚动期（`36` Tier B/C，跨版本 IPC 共存 `23` §6.4）新老进程需就 rights 语义达成一致——亚稳契约的 `rights_revision` 让 revoke / derivation tree（`07` §4.5）在跨版本灰度中可观测、可协商，避免悬垂。

### 3. 37-待决3：IPC 协议版本号归属 — 采纳双轨

**裁决结论**：IDL 命名空间与版本号归属采用**双轨**，仿 Fuchsia 平台 FIDL vs 服务私有 FIDL（`37` §5.2 / `23` §8 待决2）：

| 轨道 | 命名空间 | 版本号权威 | 承载对象 | 协商机制 |
|------|----------|------------|----------|----------|
| **平台保留轨** | `meu.platform.*`（系统保留，禁止私有占用） | **系统统一分配**（`00` / `05` 管理，保证向前兼容基线、防冲突） | 系统服务：devmgr、fs、pkg-loader、supervisor、netstack、audio-service 等（`10` / `25` / `35`） | `23` §6.1 `protocol_version` / `min_compatible_version`，Service Registry（`05` §4.4）登记 |
| **服务私有轨** | `meu.app.<owner>.*` | **服务自管**（owner 自主升版本） | 第三方 / 在树业务服务 | 同上，私有命名空间内自由演进，跨服务经各自 manifest 协商 |

- **平台轨版本号统一分配的理由**：系统服务协议是「跨 `.msys` 稳定基线」的承载者，统一分配可避免各系统服务协议号撞车、保证 MeuOS 升级时平台 IPC 面有可控的向前兼容基线（类比 Fuchsia 平台 FIDL 由 SDK 统一管控）。
- **私有轨自管的理由**：业务服务演进自由，不与平台升级节奏耦合；但其 `protocol_version` 仍须在 Service Registry 声明以便 `23` §6.2 协商与 `06` §4.3e 灰度共存。
- **IDL 工具强制**：双轨命名空间均由 `meu-idldef` 定义、`meu-codegen` 生成 stub / proxy——即「命名空间可双轨，但生成源仍单一」（回扣 待决1 唯一出口）。

**理由（解构 + 多系统参照）**：
- **Fuchsia FIDL 双命名空间**：平台 FIDL 与组件私有 FIDL 分离，平台面由 SDK 统一版本管控——直接范本（`37` §2 / `23` §3）。
- **Android Binder / 版本化接口**：跨进程接口走版本协商而非全局冻结——印证「版本协商式稳」（`23` §3）。
- **Windows COM / WinRT 契约**：接口带版本与契约元数据——印证「协议须携带版本元数据」思想。
- **与 `36` / `06` 一致性**：双轨 + Service Registry 协商，使 `36` Tier B/C 滚动更新（新老服务跨版本共存 `23` §6.4）能按命名空间区分「平台基线服务」与「业务服务」，灰度切割更干净。

### 4. 三裁决的联合收口（与 23 / 36 / 07 一致性声明）

- **稳定承诺总图（重申）**：① 兼容 ABI 稳（锚 uname）｜② 原生 syscall / API 不稳（靠 libc 屏蔽，保留破坏权）｜③ IPC 版本协商稳（本裁决 待决3 双轨）｜**亚稳契约（待决2）介于 ②③ 之间、受更严窗口**。四者全部由 `meu-idldef` 单一真相源生成（待决1）——MeuOS 因此既「该稳的稳（兼容层 + IPC + 亚稳契约）」又「该破的明破（原生 syscall）」，无 Linux 式「一刀切全稳锁死演进」包袱，也无「全不稳生态碎」风险。
- **与 `36` 热升级**：静态 TCB（Tier A）使 SDK 表面构建期固定、随 `.msys` 整体分发；Tier B/C 滚动更新靠 ③ 协商 + 亚稳契约 `rights_revision` 对齐——唯一出口纪律与「用户态组件化热升级」互为支撑，无运行期扩面需求。
- **与 `07` capability**：rights 位图经 IDL 生成（待决2），与 `38` §3 统一 rights bit 表（单一真相源）直接咬合；`LOAD` / `VERIFY` 等新增 right（`36` §4.2 待决3）同样进 IDL，不手写第二份。
- **待 `05` / `04` / `07` 协同回填**：本裁决不替 `05`（Service Registry 版本权威落地）、`04`（Job manifest 字段冻结）、`07`（rights 位图定稿）下结论，仅确认「双轨 + 亚稳 + 唯一 IDL 源」方向，待其设计级回填。

### 5. 对 00 冲突表 / 待升级候选区状态

- 37-待决1 / 37-待决2 / 37-待决3 三项**第五轮自主采纳**，状态升级为「已采纳」；`00` 待升级候选区对应项标注「已采纳（第五轮）」。
- 关联 `38` §4 候选清单中「IDL 唯一出口 / 亚稳契约覆盖 / IPC 版本号双轨」三项同步结案。

---

> 本文档状态：规划草案 v1 + 第五轮裁决回写（lite / hy3，自主采纳）。遵循修订式追加、不删；跨域项以 `00-总览与路线图.md` 的 `C1`–`C16` 及 `31` / `38` 为准。

---

## 第五轮裁决回写（IDL 唯一出口）

> 第五轮调研 agent（lite / hy3，自主采纳）依据 `38-第四轮收敛摘要.md` §4 / §5 的待升级候选 **37-待决1（IDL 唯一出口）** 与关联项 **37-待决2（亚稳契约）/ 37-待决3（IPC 版本命名空间）**，自主采纳如下。铁律：仅追加、不删改原章节；原 §1.3 / §3.2 / §5.2 待决 1–3 的「待裁决」表述升级为「已采纳」，原内容保留。
> 方法论：解构型思维（先推演「契约应该是什么样」→ 单一真相源消灭「内核头即契约」）+ 多系统参照（Fuchsia FIDL 即契约文档+SDK 唯一出口、seL4 证明即规格、Android AIDL/Treble 边界、Windows IDL/COM+WDF 版本化、D-Bus XML、Cap'n Proto）+ 现代化 + 中文。
> 关联：`07-安全模型.md`（rights 位图）、`05-IPC机制.md`（Service Registry / 协议版本）、`04-进程与调度.md`（Job manifest）、`10-设备驱动模型.md`（设备能力）、`23-ABI稳定性与版本.md`（三层稳定性 / IPC 版本协商）、`21-迁移路线图.md`（Kit 工具链 / sysroot 双重身份）。

### 采纳结论：单一 IDL（meu-idldef）= 系统能力的「唯一真相源」+「唯一生成源」

**核心裁决**：采用**单一机器可读 IDL（`meu-idldef`）**作为 MeuOS 全部系统能力的**唯一真相源（single source of truth）**与**唯一生成源（single export gate）**——syscall/服务契约（IPC 协议）、capability/rights 规格、Job manifest、设备能力规格**全部由 IDL 定义**，经 `meu-codegen`→libc/Rust 绑定、`meu-docgen`→人类文档、`meu-vergen`→版本元数据统一生成。**彻底根除「内核头即契约」的 Linux 式泄漏**。

#### 1. 唯一真相源：四类规格全部进 IDL（已采纳）

| IDL 规格类别 | 描述对象 | 生成产物 | 对应子文档 |
|--------------|----------|----------|------------|
| **capability / rights 规格** | rights 位图（基础 7 + `KEY_*` + `DBG_*` + `CFG_*` + `LOAD`/`VERIFY` 等）、对象类型→rights 约束、mint 上限 | C/Rust rights 常量头 + SDK + 文档 | `07`（§第四轮裁决回写） |
| **IPC 协议规格** | Channel 消息 schema、type tag、服务协议 `protocol_version`/`min_compatible_version` | stub/proxy 绑定 + 文档 + 协商元数据 | `05`、`23` §6 |
| **Job manifest 规格** | spawn 输入清单：资源配额 / 初始 capability 集 / namespace 模板 / `debug_policy` | manifest schema + 校验器 + 文档 | `04`、`07` §4.3 C3 |
| **设备能力规格** | 设备能力（MMIO/IRQ/DMA/IO port）schema、驱动协议 | 驱动 SDK 绑定 + 文档 | `10`、`23` §4.2 |

> IDL 改一处，三产物（头/绑定/文档）+ 版本元数据**同步生成**，根除「rights 改了头没改」「IPC 字段增删文档滞后」「Job manifest 与实现漂移」三类 Linux 教训（`37` §1.4）。

#### 2. 唯一出口边界：出树强制、内核内部可直连但类型仍由 IDL 生成（已采纳，回应待决1 张力）

- **出树开发者（Out-of-tree）：硬纪律，零例外**。所有系统能力**只经 IDL 生成的 SDK 暴露**：禁止裸 syscall 号、禁止 `#include` 内核内部头、禁止文档化泄漏内部 rights 位布局/结构体内存布局（§3.3 执行纪律 1）。开发者只见「被授权的 SDK 表面」——把 `07`「无隐式权限」延伸到文档/API 层（§1.3）。违反者不被 SDK 支持、跨 `.msys` 不保证（`23` §2）。
- **内核 TCB / 内部实现（In-tree）：可直连内部原语，但类型仍由 IDL 生成**。内核 Rust/C 为性能与正确性可**直接调用**内部原语（不强制绕一层 IDL 运行时 indirection），但 capability/rights/IPC 的**权威类型定义必须来自 IDL 生成**（或经 `meu-codegen` 产出、或与 IDL 同源校验），确保「单一真相源不漂移」——这正是原待决 1 张力的解法：**IDL 是 single source of truth，不是 single runtime indirection layer**。内部直连不破坏「真相源唯一」，因为类型定义的权威仍在 IDL。
- **出树第三方服务契约边界**（`23` §8 待决 3）：出树第三方服务要么经 libc（屏蔽原生不稳定 syscall）要么经 IPC（版本协商），**禁止直接发裸原生 syscall**——失去 `.msys` 同版本保护即违约（§3.3 执行纪律 3）。

#### 3. 亚稳契约覆盖（待决2，已采纳）：rights 位图 + Job manifest 受更严弃用窗口

- `37` 待决 2 采纳：**IDL 必须覆盖「亚稳契约」（rights 位图 / Job 账本字段）**，且因其被 SDK/调试/`12` 审计工具大量消费、跨 `.msys` 被外部工具依赖，经 IDL 生成后应**比裸 syscall 更稳**——受**更严弃用窗口**（deprecation window 长于原生 API 的「演进期明确不稳」，`23` ②）。
- 这与「原生 API 不稳靠 libc 屏蔽」（`23` ②）不冲突：原生 syscall 表面可随 `.msys` 演进，但 rights 语义 / Job manifest 字段作为「被工具广泛消费的契约」走亚稳承诺——层数分清。

#### 4. IPC 版本命名空间双轨（待决3，已采纳）：平台保留 + 服务私有

- `37` 待决 3 采纳：**IDL 工具提供「平台保留 IDL 命名空间 + 服务私有 IDL 命名空间」双轨**（仿 Fuchsia 平台 FIDL vs 服务私有 FIDL），需 `05` 协同。平台命名空间承载系统契约（syscall/IPC 协议/rights/Job manifest/设备能力），服务私有命名空间承载各服务自管协议——版本号归属双轨：`protocol_version` 系统契约由平台统一分配、服务私有协议由服务自管（`23` §6.1）。

#### 5. 工具链归属（设计协同，采纳建议）：meu-idldef 入 Kit

- `meu-idldef` / `meu-codegen` / `meu-docgen` / `meu-vergen` 归入 **Kit 工具链**（`21` §1.1，与 mcc/mt 同级），随 `.msys` sysroot 一并产出与分发；bootstrap Phase K 除产内核镜像，额外产「匹配该 `.msys` 版本的 SDK 包」（头+绑定+文档+版本元数据），实现「内核+libc+服务+SDK 同版本整体交付」（`23` §5 杠杆在 DX 维度的延伸）。IDL 工具自身自举（吃自己狗粮）路径随 Kit 演进。

### 与周边文档的衔接（交叉引用）

- **`07`**：rights 位图（含 `KEY_*`/`DBG_*`/`CFG_*`/`LOAD`/`VERIFY`）由 `meu-idldef` 生成 SDK/头/文档，单一真相源消除「位图改头没改」漂移；`07` §第四轮裁决回写已冻结的 bit 分配（`38` §3 统一表）即 IDL 的 rights 规格权威。
- **`05`/`23`**：IPC 协议 `protocol_version`/`min_compatible_version` 由 IDL 生成（`23` §6.1）；双轨命名空间需 `05` Service Registry 协同。
- **`04`/`10`**：Job manifest 与设备能力 schema 由 IDL 生成，分别供 `04` spawn 与 `10` 驱动 SDK 消费。
- **`21`/`23`**：SDK 包与 `.msys` 同版本绑定（`23` §5）；IDL 工具归 Kit（`21`）。
- **`12`**：`DBG_*` 调试权经 IDL 生成为 capability SDK，默认 deny、经 Channel 守护（`37` §4.3）。

> 范围边界：本裁决覆盖 **37-待决1（IDL 唯一出口）+ 待决2（亚稳契约）+ 待决3（IPC 版本双轨命名空间）** 三项，且将「SDK 唯一出口」由 §1.3 的建议升级为**硬纪律已采纳**。其余待决 4–7（工具链归属仓库、SDK 版本绑定粒度、驱动协议版本并入、codegen 五架构一致性）为设计/实测协同项，维持原 §5.2 协同安排、不在本裁决新开结论。
