# mt/ld riscv64 PCREL_LO12 p_hi 锚点基准 bug（+0x14 应为 −0x14）

> 状态：🔄 开放（2026-08-05 mt-worker 清矩阵 rr_global:riscv64 时定位）
> 关联：矩阵 `rr_global` riscv64 返回 0（应 42）

## 现象

- riscv64 `rr_global`（全局变量 RMW）矩阵运行返回 0（应 42）；
- 排查定位到 **mt/ld 的 `reloc.c` ~line 590**：PCREL_LO12 的 p_hi **锚点基准差 0x28**（`+0x14` 应为 `−0x14`）；
- riscv64 全局符号地址解析经 `R_RISCV_PCREL_HI20/LO12_I` 时，LO12 的符号件错取锚点，导致全局变量寻址偏移错 → 读错地址/错值。

## 精确根因（2026-08-05 调试确认）

rr_global 的 `g++/return g` 三处 `auipc %pcrel_hi(g); addi %pcrel_lo(.LrvpcN)`：
- 第一处 `.Lrvpc1`（**section offset 0**，函数首指令前）出问题：配对 HI20 relocation 的 symbol `g` 经 `symbol_value` 解析得 **S=0**（错，应 0x406000=.data），且 `.Lrvpc1` 的 resolved 地址 `p_hi=0x40106c`（错，auipc 实际在 0x401014）→ `delta=S+A−p_hi` 错 → `addi` 立即数 +0x14 而非 −0x14 → 读到 g+0x28（错地址）；
- 其余 LO12（S=0x406008/0x406010…）解析正常。**仅 section-offset-0 的本地标签出错**；
- 根因在 `reloc.c` PCREL_LO12 分支（L224-291）：`p_off=lo_sym_value`（本地标签 .Lrvpc1 的值）+ HI20 配对 `scan roff==p_off`。offset-0 本地标签解析 + HI20 配对（roff==0 可能选中错误 HI20 reloc）需 deep 符号解析修正——非一行编码，**真专项**。

## 判定

- **mt/ld 内部重定位实现实 bug**（非 mcc/mt/as——mt/as 各指令 + 编码 vs GNU 逐字节一致）；
- 属 mt/ld 对 riscv64 PC-relative（PCREL_HI20/LO12）符号解析的锚点基准错误；
- 影响 riscv64 全局/绝对地址符号在 PIE/重定位下的访问偏移。

## 范围

- `projects/meuos-toolchain/src/ld/reloc.c` 约 L590 的 PCREL_LO12 锚点基准计算（+0x14 → −0x14）；
- 需对照 riscv64 ELF psABI 的 PCREL_HI20/LO12 锚点定义修正；
- 修后 `rr_global:riscv64` 应从 xfail 移除。

## 验收

- riscv64 `rr_global` 运行返回 42（全局变量读写正确）；
- 不影响其它架构 LO12 重定位（x86_64/aarch64 等）；
- `make -C projects/meuos-toolchain check` + `check-qemu-all` 无回归。

## 排程

- mt 下阶段专项（可与 loongarch64 runtime/segfault 并行或其后）；
- 修后经验沉淀 `.agents/knowledge/`。
