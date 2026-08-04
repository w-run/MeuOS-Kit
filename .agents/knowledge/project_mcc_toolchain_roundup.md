---
name: mcc-toolchain 本轮进展汇总（TLS 闭环 / m++ P1 / libc P0.1 / 新阻塞 / 教训）
description: 动态链接 TLS 静态 GD 闭环、m++ C++ P1 5 项、P0.1 动态 libc、mt/ld .dynamic 新阻塞、静态数组 segfault、关键教训
type: project
---

# mcc-toolchain 本轮进展汇总（2026-08-04/05 并行提速轮）

> 与 `project_mcc_toolchain.md` 互补：该文件承载 P0/P2 基线，本文件承载本轮并行推进的多个新里程碑与发现。

## 1. P1 动态链接 TLS 静态 GD→LE 运行期闭环（合入主开发分支）

- **D1** `f65590c`（mt/as）：`@PLT` 大小写——只认小写 "plt"，mcc 发大写 `@PLT` 落 PC32(2) 非 PLT32(4)；改同时匹配 "plt"/"PLT"。
- **D2** `d0b1e70`（mt/ld）：PLT32→**JUMP_SLOT 收集**——`collect_got_relocations` 不收集未定义 PLT32 import，`__tls_get_addr` 无 JUMP_SLOT 落地址 0。
- **D3** `4e54505`（mcc x86_64）：GD **store clobber**——`tvar=v` 在 `call __tls_get_addr` 前值放 caller-saved(rax) 被 call clobber；mreg_pool_build 预留 %rbx 跨 call 保 store 值。
- **memsz / .comment 根治** `07303b2`（mt/ld）：TLS 布局移到 non-alloc 前，`.comment` 不再映射进 .bss；`PT_TLS filesz=4 memsz=8 align=8`。
- **方案 B** `ae88aa1`（mt/ld）：**静态 tls_index descriptor**——ET_EXEC 每 R_X86_64_TLSGD 在 .data 生成 16B `{ti_module=1, ti_offset=tpoff}`，`lea sym@tlsgd(%rip)` 指向它，保留 `call __tls_get_addr`（libc 静态）。
- **结果**：LE / GD 读写 / GD 只读 三用例 exit 0（原 GD 两用例 139）；多 TLS（thread+errno）exit 0。
- **⚠️ 假阳性陷阱**：测 GD TLS 必须用 **P1a 分支 mcc**（含 D3 `4e54505`）；主线 mcc 在 `-ftls-model=global-dynamic` 下**静默退化成 LE**，掩盖 GD 实测（见教训§6）。

## 2. m++ C++ 前端 P1 5 项（合入主分支）

- `c9fc901` decltype type specifier（C++11）。
- `e68446d` scoped enum / enum class（C++11）。
- `d978c59` default template arguments。
- `4240a42` fold expressions（C++17）。
- `39dea30` constexpr aggregate return folding。
- `59313f5` 将已解决的 pending 用例提升至 main tests（伴随清理）。

## 3. P0.1 动态 libc 工程样板（commit `d224248`，exec-libc-lite）

- meuos-libc Makefile 新增独立 `-fPIC` 对象集 `build/<arch>/pic/` 构建 `libc-meuos.so`，静态 `.a` 对象不共用，零干扰。
- `.so` 规则：`$(LD) -shared -soname libc-meuos.so.1`。
- **socketcall 排除**：`src/syscall/socketcall.c` 与 `src/socket/socket.c` 同符号，.so 全量合并冲突 → pic/ 对象集 `filter-out` 掉 socketcall。
- **environ 动态槽**：新增 `src/internal/environ.c`（`char **environ;`）仅 .so 用；静态 .a 仍由 crt1 提供。
- **回归门控**：`ifneq($(wildcard $(LD)))` 使 .so 仅 mt/ld 存在时接入 all/install，无 mt/ld 时不回归。

## 4. P0.1 新阻塞：mt/ld 大规模 .so `.dynamic` 内容损坏（已派 exec-toolchain-gp 修）

- `readelf -d`（mt 与 host 均）对 libc-meuos.so `.dynamic` 只读出 2 条、首 tag=`0x2b5b8`（非法 DT_*）；SONAME 已正确写 .dynstr，但 `{tag,val}` 区写坏。
- `.dynamic` 大小 0xa0=10 条与 ntags(SYMTAB..HASH,SONAME,RELA*3,NULL)吻合，内容错乱；**仅完整 libc.so 复现，小/中 .so 正常**。
- 嫌疑：`link.c ensure_dynamic_section(ntags)` 与 `fill_dynamic_addresses(dp 写回)` 在 .got 紧邻 / .rela.dyn 大量时错位；`0x2b5b8` 疑 .dynamic 组地址被误当 tag。
- 阻塞 P0.1 的 readelf -d SONAME 验收；P0.2/P0.3（rtld 加载 / dlopen+DTV）为硬前置，优先排期。

## 5. 静态全局数组 segfault 既有缺陷（exec-toolchain-d5 发现，已派 exec-mcc-gp 排查）

- 静态 exe + 全局数组（`int gdata[4]={10,20,30,40}`）运行 segfault(139)，**无论有无 TLS**。
- 基线 `07303b2` 亦复现（非本方案引入）。定位：mcc/mt 对 **R_X86_64_PC32 / 绝对数组寻址**组合的既有 bug，崩溃点在 crt `.fini`/`.init_array` 或 main 数组访问。
- 已登记待办 `.todo/mcc/static-global-array-segfault.md`。

## 6. 重要教训

- **测 GD TLS 必须用 P1a 分支 mcc**（含 D3 `4e54505`）：主线 mcc 无 GD 生成，`-ftls-model=global-dynamic` 静默退化成 LE 假阳性，会误判 GD 已闭环。
- **exec-toolchain-lite 状态被污染**：已 terminate，由其导致的中间产物/旧 hash（如 memory 里 `fc8aee8`）作废，以实际 commit（`ae88aa1`）为准，新 worker 重做。
- **不要自动切 tmux 主线模型**：切主模型会致 tmux 卡死；改用**紧急 notify** 通知，不阻塞会话。
- **429 时 dsv4flash fallback 已实测有效**：限流时回退到 DeepSeek-V4-Flash 保持并行不中断。
- **GCC14 宿主兼容** `47f5f70`（mt）：修复 gcc14 `-Werror=format/implicit-declaration`（PRIx64 / rename 等）基线编译错误，保证宿主 GCC14 下 mt 全量可编译。

## Why / How to apply

- 静态 GD TLS 运行期已全链闭环（mcc 生成 → mt/as → mt/ld descriptor → 静态 __tls_get_addr），动态共享运行期仍待 P0.1（动态 libc）与 mt/ld .dynamic 修复。
- 续接时先查本文件与 `project_mcc_toolchain.md` 确认对应 commit，勿重做推进项（TLS 静态 GD 已闭环、m++ 5 项已落地）。
- 新阻塞优先级：mt/ld .dynamic 损坏（阻塞 readelf -d SONAME 与 P0.2/P0.3）→ P0.1 dynamic libc 验收 → P0.2/P0.3 dlopen+DTV。
