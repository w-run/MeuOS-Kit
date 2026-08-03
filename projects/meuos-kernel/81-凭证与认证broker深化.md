# 81 - 凭证与认证 Broker 深化（Credentials & Authentication Broker Deepening）

> 子领域：身份 / 凭证 / 认证 broker（用户系统·深化维度）
> 团队：`kernel-plan`（拓展维度调研员，lite / hy3 产出）。
> 关联文档：`00-总览与路线图`、`07-安全模型`（capability Day 1、`AUTH_ISSUE`=bit55 冻结、`§4.5` 受约束 mint）、`52-用户系统-方案A`、`53-用户系统-方案B`（§8-5 `AUTH_ISSUE`、认证 broker）、`29-威胁模型`（T6/T7、confused-deputy）、`65-安全监控与审计`（能力审计）。
>
> **百思纪律（铁律）**：本文只写设计/规划文档，不写实现代码；结论可修订式追加。本文建立在以下已决地基之上——capability Day 1 唯一权限模型（`07`）、身份 = capability 集合 + issuer 私钥签注符号名（`52`/`53` 已采纳）、认证 broker 是受约束 issuer、经 `AUTH_ISSUE`(bit55) 受限委托（第八轮/第九轮已冻结为默认 deny、Day 1 不触发）、凭证 mint/revoke 与 capability 派生/回收同源（`07` §4.5）、认证事件可审计（`65`）、五架构同源。**本文不引入新内核原语、不新增 right bit（复用 `AUTH_ISSUE`=bit55 已冻结）；跨域以 `00` `A1`–`A12` / `C1`–`C18` 为准。**
>
> 方法论铁律贯穿：解构型思维、多系统参照（OAuth2/OIDC、SPIFFE、macaroon、Cap'n Proto capability）、非缝合怪、修订式追加不删。

---

## 0. 核心结论（一句话）

> **身份 = capability 集合 + 由 issuer 私钥签注的符号名（`52`/`53` 已采纳）；认证 broker 是受约束 issuer，经 `AUTH_ISSUE`(bit55) 受限委托（第九轮/第八轮已冻结为默认 deny、Day 1 不触发）；凭证 mint/revoke 与 capability 派生/回收同源（`07` §4.5，吊销即 capability 回收）；认证事件可审计（`65`）；五架构同源。**

---

## 1. 身份模型回顾（capability 集合 + issuer 签注符号名）

> 本节复述 `52`/`53` 已采纳的身份模型，作为认证 broker 设计的地基，不重判、不新立。

### 1.1 真实身份 = capability 集合（内核唯一真相源）

- `07` §1/§4.1 已定：权限 = 不可伪造的句柄（handle = capability）+ 随句柄走的 rights 位图；**无 root、无 uid=0 逃生舱**。`52`/`53` 内核模型 100% 一致——进程/会话的真实身份 = 其持有的全部 `handle + rights` 集合。
- 符号名（如 `alice`、`svc-netstack`、`role-builder`）在 capability 模型里**不承担任何裁决权**——它只是给人、给日志、给策略清单读的标识（与 `53` §2.2 合成凭据视图「零权限附着」同源）。内核不认识符号名，只认识句柄。

### 1.2 符号名 = issuer 私钥签注的 capability 附属声明

- `52`/`53` 已采纳「身份 = capability 集合 + 由 issuer 私钥签注的符号名」：符号名不是进程自报的明文标签，而是**由某个受信任 issuer 用其私钥对 `(symbolic_name, subject_capability_anchor, validity, scope)` 作密码学签注**的附属声明（credential/token）。
- 签注的意义：**内核不须信任符号名本身**（内核本就不认符号名），但用户态 broker / 策略清单可凭 issuer 公钥验证「该符号名确实由某 issuer 授权绑定到某 capability 锚」——把「身份标识」与「权限事实（capability）」解耦又关联。
- **与 capability 不变量同源**：符号名签注**不放大任何 rights**，只是把「可读标识」绑定到既有的 capability 锚；issuer 私钥本身不授予任何内核权限（与 `07` 高危权「永不默认、可审计、可 revoke」同构）。

### 1.3 与 `53` C-G4 的衔接

- `53` §7.5（第七轮精细裁决 C-G4）已闭合并明确「多 realm 边界 = capability 派生边界」「合成 uid 视图仅展示层、零权限附着」。本文的 issuer 签注符号名是 C-G4 之下的「标识层」深化——跨 realm 不因符号名而穿透，符号名作用域严格随其绑定的 capability 作用域（realm）走。
- issuer 私钥托管本身落在 realm 边界内：一个 realm 的 issuer 私钥**不**为其 realm 之外签注（受 `06` C3 / `53` C-G5 的 default-deny reveal/grant 纪律约束）。

---

## 2. 认证 Broker 角色（`AUTH_ISSUE`=bit55 门禁）

> 本节定义认证 broker 在 MeuOS capability 模型中的精确位置：受约束 issuer，不复用 `GRANT`，默认 deny。

### 2.1 认证 broker 是什么（与 `53` §2.5 一致）

- 认证 broker 是一个受 `meu-supervisor` 管理的**用户态服务**，职责 = 验证主体提交的秘密/证明（口令、硬件令牌、密钥、SPIFFE/macaroon 式 bearer），成功后 mint 一份**受限 capability 集合**并贴一张 **issuer 签注的符号名**，派生一个新会话 Job。
- 它**不复用 `GRANT` 语义**：`GRANT`(bit4) 是「跨进程授予已有句柄」的通用 right，含「转交既有权限」之意；认证 broker 的权限是「依策略清单**新增**一份会话 capability 集合 + 签注符号名」，语义更窄、更受约束，故 `53` §8-5 已裁决为独立 right（类比 `DBG_POKE`/`debug-issuer` 受约束 issuer）。

### 2.2 `AUTH_ISSUE`=bit55（已冻结，默认 deny，Day 1 不触发）

| 项 | 值（来源 `07` §4.1 第八轮冻结分配表） |
|---|---|
| bit | **55** |
| right | `AUTH_ISSUE` |
| 子系统 | 用户系统（`53` §8-5） |
| 语义 | 认证 broker 受约束 issuer right（类比 `DBG_POKE`/`debug-issuer`，防普通 `GRANT` 复用为认证授权） |
| 默认 | **deny** |
| 状态 | **已裁决·闭环**（第八轮/第九轮冻结分配；Day 1 不触发） |

- **门禁语义**：只有持 `AUTH_ISSUE`(bit55) 的 broker 句柄，才被 capability 监视器允许执行「认证 → mint 会话 capability 集合 + 签注符号名」这一受约束 issuer 操作。普通 `GRANT` 句柄**不能**跨域成为「认证授权」语义来源（破 confused deputy，呼应 `07` §4.5 / `29` T4）。
- **默认 deny + Day 1 不触发**：与 `ATTEST`=bit39、`MEDIA_CAPTURE` 等 bit48+ 预留项同构——bit55 **已占名、已冻结、但 Day 1 不分配句柄、不触发**。单用户自举期（init 直接持初始 capability 集，`53` §5.1）broker 可省略；多用户登录/认证场景（M2+）才需 broker 持 `AUTH_ISSUE` 经 supervisor 显式授予。**不引入任何新内核原语、不挤占 bit 0–47。**

### 2.3 受约束 issuer 的 mint 上限（与 `07` §4.5 同构）

- broker mint 会话 capability 集合时，严格「**broker 自身 rights 上限 ∩ 策略清单下限**」交集（`07` §4.5 受约束 mint；`53` §2.5），符号名**不参与**交集计算（符号名零权限附着）。
- 策略清单（issuer 侧）声明「身份名 ↔ 符号名 ↔ capability 模板」映射，用户态持有、不在内核；broker 据策略清单 + 验证结果决定 mint 内容，内核监视器只校验「broker 是否持 `AUTH_ISSUE` + 是否越自身上限」。

---

## 3. 凭证 mint/revoke 与 capability 派生/回收同源（`07` §4.5）

> 本节是本文核心深化点：issuer 签注的**凭证（符号名声明）**与其绑定的 **capability 句柄**是同一派生树上的两个面，吊销即回收，同源闭环。

### 3.1 同源模型（一张图）

```
issuer 私钥 ──签注──▶ credential(symbolic_name, cap_anchor, validity, scope)
                              │
                              │ 绑定（同一派生事件）
                              ▼
            认证 broker 经 AUTH_ISSUE(bit55) mint 会话 capability 集合（受约束 mint）
                              │
                              ├─ capability 句柄（内核对象，rights 收窄，随 Job 生命周期）
                              └─ 符号名声明（issuer 签注，附属于该 capability 锚）
                              │
            ┌─────────────── 派生树（derivation tree, 07 §4.5 / 29 I5）───────────────┐
            │  credential.revoke  ──▶ capability.revoke（同事件，同源）             │
            │  capability.derive  ──▶ credential 作用域随派生收窄（scope 同步收缩）  │
            └────────────────────────────────────────────────────────────────────┘
```

### 3.2 吊销即 capability 回收（闭环不变量）

- **单一事实源**：凭证的「有效」状态不是独立数据库字段，而是「其绑定的 capability 锚是否仍被内核持有/未 revoke」。`07` §4.5 的 revoke（关闭句柄、I2 派生全断）即凭证吊销——**吊销一个 issuer 凭证 = revoke 其绑定的 capability 句柄**，二者是同一内核操作的两种视角。
- **派生链同步**：凭证随 capability 派生而派生（子会话持父 capability 子集 + 收窄的符号名 scope）；revoke 一个凭证，其下所有派生 credential 经 derivation tree（I2/I5）一并失效——与 `07` §4.5「revoke 后不存在可用派生句柄」同源。
- **issuer 私钥泄露的收敛半径**：若 issuer 私钥泄露，攻击者可伪造符号名签注，但**仍须经 broker 持 `AUTH_ISSUE` 才能 mint 真实 capability**——符号名伪造本身不开任何内核权限（符号名零权限附着）。收敛半径 = broker 的 `AUTH_ISSUE` 受约束 mint 上限，而非「全局身份后门」（对比 Linux root/keyring 泄露即全盘失守）。

### 3.3 与 `07` §4.5 既定语义的严格对齐

- 不新增 revoke 原语：复用 `close`/`revoke`（`07` §4.5）+ derivation tree（`29` I2/I5）。
- 不新增 mint 原语：复用 `AUTH_ISSUE`(bit55) 受约束 issuer 路径 + 策略清单下限交集。
- 不新增 right bit：bit55 已冻结；本文仅将其语义在「凭证/符号名」维度操作化，不扩展位图。

---

## 4. 认证事件审计（`65` 能力审计）

> 本节把认证 broker 活动接入 `65` 审计模型，字段对齐 `65` §3.1 的 inspect 子树 schema。

### 4.1 认证事件 = 能力审计的子集（与 `65` 同构）

- `65` §3.1 已定：审计日志是 `kernel/audit` inspect 子树，主键为 **句柄 + 所属 Job + rights 摘要**（非身份）。认证 broker 的每次 `AUTH_ISSUE` 受约束 mint 天然是 `65` 的「受约束 mint/grant/revoke」审计事件源（`65` §3.1 发射点）。
- 认证事件记录字段（在 `65` 既有 schema 上扩展 issuer/target/right 维度，不新增内核对象）：

| 字段 | 含义 | 来源 |
|---|---|---|
| `origin_job` | broker 所属 Job | `65` §3.1（同 `04` Job 账本） |
| `subject_handle` | 被 mint 的会话 capability 句柄 | `65` §3.1 |
| `op` | `auth_issue` / `auth_revoke` | 本文扩展 op 枚举 |
| `rights_mask` | mint 出的 rights 摘要 | `65` §3.1 |
| `issuer` | 签注符号名的 issuer 公钥标识（非内核权限） | 本文扩展 |
| `target` | 被签注符号名（如 `alice`）/ 绑定的 cap_anchor | 本文扩展（`65` §3.1 `target` 字段复用） |
| `verdict` | 通过 / 拒绝（含拒绝原因：超上限、策略不匹配） | `65` §3.1 |

### 4.2 高危权审计纪律（与 `65` §3.1 同源）

- `AUTH_ISSUE`(bit55) 的**任何显式授予、使用、revoke** 必记审计——与 `DBG_POKE`/`KERNEL_PRIV`「高危权永不默认、必留痕」范式完全相同（`65` §3.1 / `29` D-T1 / `17` C9-A）。
- broker 越权尝试（试图 mint 超自身上限的 capability 集合）= `check_rights` 拒绝事件（`65` §3.1 发射点）+ `07` §4.5 I5 受约束 mint 判定，直接进审计 ring，异常检测标记「认证 mint 风暴」（`65` §5.1 权限提升尝试类）。

### 4.3 认证审计的读取权限（沿用 `65` S1 闭环）

- 认证审计 ring 是 `kernel/audit` inspect 子树的一部分，**Day 1 复用 `DBG_INSPECT`(bit13) 读取**（第九轮 S1 已闭环方案 A），专属 `AUDIT` right 仅预留 bit48+ 不分配。本文不重开 S1。

---

## 5. 威胁模型衔接（`29` T6/T7；confused-deputy 防护）

> 本节把认证 broker 接入 `29` 威胁分类，证明其在既有纵深内可挡，不引入新威胁类。

### 5.1 confused-deputy（T4）防护的精确落点

- **威胁**：攻击者诱导持 `AUTH_ISSUE` 的 broker 代自己 mint 超权限会话（经典 confused deputy，`29` §3.1 T4 / `07` §4.5）。
- **挡点**：`07` §4.5 I5 受约束 mint（`rights_issued = rights_broker ∩ rights_policy`）——broker 既不能 mint 超自身上限、也不能 mint 超策略清单授权；符号名签注**不参与**裁决，故攻击者无法借「伪造高特权符号名」骗取权限。这与 `29` T4 既有挡点完全一致，本文只是把「认证 mint」显式纳入该挡点。

### 5.2 调试权滥用类（T6/T7）的旁挂

- `AUTH_ISSUE`(bit55) 与 `DBG_POKE`(bit18) 同构为「受约束 issuer 高危权」：绝不默认、绝不随普通 `GRANT` 自动获得、授予可审计可 revoke（`07` §4.1 / `17` C9-A）。
- broker 崩溃/冻结劫持（类比 T7）：broker 是用户态服务，崩溃经 `04` §4.5 监督者 exception channel 重启，已 mint 的会话 capability 随 Job 取消/重绑（与 `63` §9.3 N10 同构）；监督者决策不可抑制（`17` C9-B / `29` T7）。

### 5.3 攻击面收敛结论

- 认证 broker 的 T-A 爆炸半径 = 其 `AUTH_ISSUE` 受约束 mint 上限（capability 边界），**不**因「身份/符号名」维度而放大。攻击者即便攻破 broker，其可 mint 的会话集也被 broker 自身 rights + 策略清单双重上限锁死——与 `29` §1「攻破一个进程的爆炸半径 = 该进程持有的句柄边界」同源。

---

## 6. 五架构同源；与既有地基同构（不引入新内核原语）

> 本节收口：认证 broker 模型在五架构上逻辑一致，且不引入任何新内核原语或新 right bit。

### 6.1 五架构同源

- 认证 broker 是**纯用户态服务**，其逻辑（验证秘密、查策略清单、经 `AUTH_ISSUE` 受约束 mint、issuer 签注符号名）不依赖任何架构页表/中断/MMIO 语义，与 `63` §7「netstack 同源 Rust」同口径。
- `AUTH_ISSUE`(bit55) 的 rights 位图语义在 x86_64 / i386 / aarch64 / riscv64 / loongarch64 下完全一致——rights 位是架构无关的 capability 抽象（`07` §4.1）。
- i386 纯 C 约束（`02` C2）**不触及** broker：broker 是用户态 Rust/服务组件，与 NIC 驱动 C 后端（`63` §7）不同，无任何新架构分支。

### 6.2 与既有地基同构（不引入新内核原语）

- **不引入新内核对象**：认证 broker 复用既有内核对象（Job 树 `04`、句柄/capability `07`、inspect 审计子树 `65`/`12`）。
- **不新增 right bit**：仅复用已冻结的 `AUTH_ISSUE`=bit55 + 既有 `DBG_INSPECT`(bit13)（读审计）+ 通用 `GRANT`/`REVOKE`/`MANAGE`（capability 生命周期）——零新位分配。
- **与 `52`/`53`/`07`/`29`/`65` 同构闭环**：身份=capability+issuer 签注符号名（`52`/`53`）→ broker 受约束 issuer（`07` §4.5 / bit55）→ 凭证/revoke 与 capability 回收同源（`07` §4.5）→ 认证事件审计（`65`）→ 威胁挡点（`29` T4/T6/T7）。全链无新原语。

---

## 7. 待决项表（标注是否需大喵拍板）

> 下列为本文新提的待决/协同项；跨域项以 `00` 冲突登记表 C1–C18 为准。

| 编号 | 待决项 | 是否需大喵拍板 | 关联 |
|------|--------|----------------|------|
| **P1** | **认证 broker 是否单一系统服务**（类比 `63` N13 netstack 单例）？多 broker 并存是否导致 issuer 身份歧义 / 凭证溯源失真 | 否（协同 `04`/`53` 定稿，N13 已闭环单例范式可借鉴） | `53` §2.5、`63` N13、`04` 监督 |
| **P2** | **issuer 根密钥托管**：issuer 私钥由谁持有、如何轮换、泄露如何吊销（与 `32` 度量基 / `09` Stage 4 根信任锚协同） | 已裁决·reasoning 决策已出（2026-08-03·待大喵复核） | `32` §3.3、`61` `.mimg`、`09` Stage 4 |
| **P3** | **`AUTH_ISSUE` Day 1 触发条件**：何时从「预留不触发」升格为「broker 持 bit55 启用」（单用户自举 vs 多用户登录的切换点） | 否（执行层定，沿用 bit55 已冻结「Day 1 不触发」立场） | `07` §4.1 bit55、`53` §5.1 |
| **P4** | **凭证吊销传播延迟**：revoke 经 derivation tree 全量回收的时序一致性（与 `29` T5 revoke 竞态、derivation tree 全量与否协同） | 否（归 `07`/`20`/`29` D-T3-D-T5） | `07` §4.5、`29` T5、D-T3、D-T8 |
| **P5** | **跨 realm 认证边界**：一个 realm 的 issuer 签注符号名是否可被另一 realm 信任（federation / cross-realm trust），还是严格 default-deny（`53` C-G4 同构） | 已裁决·reasoning 决策已出（2026-08-03·待大喵复核） | `53` §7.5 C-G4、`06` C3、`00` C1–C18 |
| **P6** | **符号名 scope 与 capability 派生同步的精确规则**：派生子会话时符号名 scope 如何随 capability 收窄自动收缩（防 scope 膨胀） | 否（协同 `07` §4.5 / `53` 定稿） | `07` §4.5、`53` §2.2 |
| **P7** | **认证事件审计的 `op` 枚举扩展**：`65` §3.1 schema 是否接纳本文 `auth_issue`/`auth_revoke` 等 op 与 `issuer`/`target` 字段 | 否（协同 `65` 修订式追加） | `65` §3.1、`12` inspect |
| **P8** | **bearer 凭证格式选型**（SPIFFE SVID / macaroon / 自研 capability token）：仅影响用户态 broker 互操作，不进内核 | 否（设计层，多系统参照定） | 见 §8 参考文献 |

> 跨域分歧以 `00` 冲突登记表 C1–C18 为准：P2（issuer 根密钥托管）与 P5（跨 realm 认证边界）需大喵架构级拍板；其余为执行层/`07`/`53`/`65` 协同定稿项。

---

## 8. 参考文献（真实 URL，已核验可访问）

### 身份 / 认证协议（对标 issuer 模型）
- OAuth 2.0（Authorization Framework，bearer token / authorization server 角色）. https://oauth.net/2/
- OpenID Connect Core 1.0（ID Token = issuer 签注的身份声明，类比本文符号名签注）. https://openid.net/specs/openid-connect-core-1_0.html
- SPIFFE / SPIRE（工作负载身份 = issuer（CA）签注的 SVID，无内核 uid 范式，直接对标 broker 受约束 issuer）. https://spiffe.io/docs/latest/spiffe-about/
- macaroon（带 attenuation/caveat 的 bearer 凭证，capability 式委托，类比本文「派生即 scope 收窄」）. https://ai.google/research/pubs/pub41892 （论文：Birman et al., *Macaroons: Cookies with Contextual Caveats*）；项目页 https://github.com/rescrv/libmacaroons

### Capability 模型（同源理论基底）
- Cap'n Proto（capability 式 RPC，权限随引用传递，对标本文 credential/capability 同源）. https://capnproto.org/cxxrpc.html
- Dennis, J. B. & Van Horn, E. C. *Programming Semantics for Multiprogrammed Computations*. CACM 9(3), 1966. https://dl.acm.org/doi/10.1145/365230.365252
- Hardy, N. *The Confused Deputy*. ACM SIGOPS OSR 22(4), 1988. https://dl.acm.org/doi/10.1145/54289.871709
- Klein, G. et al. *seL4: Formal Verification of an OS Kernel*. SOSP 2009. https://www.sigops.org/s/conferences/sosp/2009/papers/klein-sosp09.pdf

### 密钥托管 / 信任锚（P2 关联）
- TPM 2.0（密钥托管与签名 quote，issuer 根密钥可驻留硬件锚）. https://trustedcomputinggroup.org/resource/tpm-library-specification/
- Android Key/Test Attestation（硬件签名 quote 范本，对标 issuer 私钥出核托管）. https://source.android.com/docs/security/key-attestation

### 项目内（已读，跨域对齐）
- `00-总览与路线图.md`（A1–A12、C1–C18、lean hybrid 收口）
- `07-安全模型.md`（capability/rights 位图 §4.1、`AUTH_ISSUE`=bit55、受约束 mint §4.5、pledge）
- `52-用户系统-方案A.md`（无 UID/GID 能力身份、身份=capability+issuer 签注符号名）
- `53-用户系统-方案B.md`（§2.5 认证 broker、§7.5 C-G4 多 realm 边界、§8-5 `AUTH_ISSUE`=bit55 闭环）
- `29-威胁模型.md`（T4 confused deputy、T6/T7 调试权、D-T1 高危权永不默认）
- `65-安全监控与审计.md`（能力审计 schema §3.1、S1 `DBG_INSPECT` 闭环、运行时异常检测 §5）
- `32-安全启动与可信计算.md`（度量基双锚、issuer 根信任锚协同点）
- `61-内核自举与早期启动深化.md`（`.mimg` 四跳信任链、`PCR[1..4]` 绑定）

---

## §8 第十轮自主采纳裁决（待大喵复核）

> 本节按第十轮收口指挥官纪律——「文档内所有『需大喵拍板』项一律先自主采纳、待大喵复核」——闭环 §7 待决项表中架构级 P2 / P5 两条。立场陈述而非新设计，与既有地基（`07` §4.5、bit55 已冻结、`53` C-G4、`32`/`61`/`09` 信任链）严格同构。

**P2：issuer 根密钥托管**

- 采纳立场：**issuer 根密钥托管于内核 secure-key 区、经 `AUTH_ISSUE`(bit55) 受约束委托，默认 deny、Day 1 不触发**。
- 落地要点：
  - 根密钥生成在 `09` Stage 4 信任锚初始化阶段完成（与 `32` §3.3 度量基双锚、`61` `.mimg` 四跳信任链同源绑点），私钥不落用户态、永不导出 secure-key 区。
  - broker 仅持 `AUTH_ISSUE`(bit55) 受约束 issuer 句柄，凭此向 secure-key 区申请「签注符号名」委托；委托动作受 `07` §4.5 受约束 mint 上限（broker 自身 rights ∩ 策略清单下限）约束。
  - 轮换 / 泄露吊销：通过 secure-key 区原语触发 derivation tree 全断（`07` §4.5 / `29` I5），所有已签注 credential 同步失效，与凭证吊销同源（§3.2）；不新增内核原语。
  - 与 `32`/`61`/`09` 协同点继续以 `00` 跨域项 C1–C18 登记为准，本文不重判信任锚架构。

**P5：跨 realm 认证边界**

- 采纳立场：**跨 realm 认证默认按 realm 边界 deny，issuer 签注跨 realm 须显式 realm 间信任约定，复用第八轮多 realm 边界同构**。
- 落地要点：
  - 默认 deny：一个 realm 的 issuer 私钥**不**为 realm 外签注符号名（与 `53` §7.5 C-G4 / `06` C3 default-deny reveal/grant 纪律严格同构）；broker 跨 realm 转交须由双方 realm 监督者通过 realm 间信任约定显式授权。
  - 信任约定形式：复用既有 realm 间边界——显式跨 realm trust 表（policy list），broker 据此表 + 双方 issuer 公钥验证签注链；不引入新内核原语，跨 realm 信任表存于用户态、不进内核。
  - 符号名 scope 严格随其绑定 capability 的 realm 作用域走：跨 realm 不因符号名而穿透（`53` §7.5「零权限附着」同构）。
  - 与 `00` A1–A12 / C1–C18 跨域项保持登记一致；任何例外仍需 `00` 跨域冲突登记流程。

**reasoning 决策（2026-08-03）—— P2**：**维持「issuer 根密钥托管于内核 secure-key 区 + 经 `AUTH_ISSUE`(bit55) 受约束委托、默认 deny、Day 1 不触发」**（同意指挥官原占位）。理由——(1) **TCB 体量**：用户态托管方案把 issuer 私钥落在 broker 进程空间，broker 自身崩溃/被攻破 = 私钥泄露，T-A 爆炸半径扩大到 broker 进程 + 私钥覆盖的所有会话（违反 §3.2「issuer 私钥泄露收敛半径」论点——收敛半径应 = broker 的 `AUTH_ISSUE` 受约束 mint 上限，而非 broker 进程 + 私钥之并集）；硬件 TPM/密钥岛内部托管方案虽然私钥永不导出硬件，但 Day 1 不一定所有架构都有硬件密钥岛（i386 软实现必须降级，与 `32` D-SB8 / `78` §6.2 同源）——内核 secure-key 区 + `AUTH_ISSUE` 受约束委托路径在五架构上完全同源（与 `78` §6.2「密钥材料永驻 secure-memory」同构）。(2) **安全模型自洽性**：issuer 根密钥是"长期密钥材料"，按 `78` §2「密钥材料永驻 secure-memory / 永不出密钥守护 pager」承载纪律——broker 类比为"密钥守护在认证维度的对偶"，仅持受约束 issuer 句柄（与 `KEY_*` 服务端句柄同构），不持私钥本身；`AUTH_ISSUE`(bit55) Day 1 不触发 = 与单用户自举期不引入 broker 一致；bit55 已冻结、复用 `07` §4.5 受约束 mint 范式，零新 right bit。(3) **信任链协同**：根密钥生成在 `09` Stage 4 信任锚初始化阶段完成（与 `32` §3.3 度量基双锚、`61` `.mimg` 四跳信任链同源绑点），私钥不落用户态、永不导出 secure-key 区；轮换/泄露吊销通过 secure-key 区原语触发 derivation tree 全断（`07` §4.5 / `29` I5），所有已签注 credential 同步失效，与凭证吊销同源（§3.2）。(4) **长期可维护性**：与 `78` 密钥后端（硬件密钥岛 / TPM / 软实现）共用 secure-key 区的五架构同源薄层（`78` §6.1），未来若需要"硬件密钥岛托管 issuer 根密钥"作为加固项，可在不破内核 API 的前提下叠加（薄层扩展、内核不感知）；用户态托管方案在加固项上反而要重写 broker。被否决选项：用户态托管（broker 崩溃即私钥泄露 / T-A 爆炸半径放大）/ 硬件密钥岛内部直接托管（Day 1 五架构不齐全 / 与文件加密 KEK 混用增加密钥岛语义负担 / broker 必须经密钥岛能力调用路径更长）。

**reasoning 决策（2026-08-03）—— P5**：**维持「跨 realm 默认 deny、issuer 签注跨 realm 须显式 realm 间信任约定」**（同意指挥官原占位）。理由——(1) **TCB 体量**：跨 realm 默认允许意味着 capability 监视器需要在"认证路径"上加跨 realm 联邦策略引擎——「哪些 realm 对之间可联邦 / 哪些符号名可跨」，等价于把"联邦图"塞进内核；default-deny + 显式 realm 间信任约定（用户态 policy list）让 capability 监视器只对 realm 内 broker 做 `AUTH_ISSUE` 受约束 mint 授权，跨 realm 决策全在用户态，路径极简。(2) **安全模型自洽性**：issuer 私钥的 realm 局部性是身份层与 capability 层对齐的关键（与 §1.3「issuer 私钥托管本身落在 realm 边界内」严格同构）；跨 realm 联邦让 issuer 私钥覆盖范围突破其 realm，与 `53` §7.5 C-G4 / `06` C3 default-deny reveal/grant 纪律直接冲突；federation（OAuth / SPIFFE federation / SAML）场景应通过"双方 realm 监督者经信任约定显式授权 + broker 据策略表验签链"实现，不破 default-deny——这与 `63` netstack 单例监督服务 + `00` A1–A12 / C1–C18 跨域项保留例外通道的同源纪律一致。(3) **威胁模型**：跨 realm 联邦一旦允许，issuer 私钥泄露的 T-A 爆炸半径从"realm 内"放大到"realm 联邦图内"，与 `29` §1「攻破一个进程的爆炸半径 = 该进程持有的句柄边界」原则直接冲突；default-deny 让爆炸半径严格圈定在 realm 内。(4) **长期可维护性**：未来若需要"特定 realm 对之间联邦"——由双方 realm 监督者经 realm 间信任约定（policy list）显式授权，broker 据策略表 + 双方 issuer 公钥验证签注链（`65` 审计主键 `origin_job + subject_handle + op + rights_mask + issuer + target` 完整记录联邦事件），不动内核。被否决选项：跨 realm 默认允许（issuer 私钥覆盖范围突破 realm / 与 C-G4 / C3 default-deny 纪律冲突 / T-A 爆炸半径放大）。

---

> 本文为规划草案，不修改 `00`–`77`/README/git；结论供执行层与 `53`/`07`/`29`/`65`/`52` 引用。跨域项以 `00` 总览 `A1`–`A12` / `C1`–`C18` 为准；本文不引入新内核原语、不新增 right bit（复用 `AUTH_ISSUE`=bit55 已冻结）；五架构同源。待决项 P1–P8 中 P2（issuer 根密钥托管）、P5（跨 realm 认证边界）按第十轮自主采纳立场闭环（§8），待大喵复核；其余为执行层/`07`/`53`/`65` 协同定稿项。
