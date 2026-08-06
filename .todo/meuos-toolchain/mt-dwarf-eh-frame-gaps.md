# mt DWARF .eh_frame（异常处理展开信息）完整化缺口清单

> 状态：🔄 开放（2026-08-06 首轮调研）
> 范围：mt/as（`as_dwarf.c` + `as_parse.c`）+ mt/ld（`elfout.c` .eh_frame_hdr）
> 关联：mcc 异常处理（C++ 异常、setjmp/longjmp 回溯 unwind）

## 背景

mt/as 已实现 `.debug_line` 行号表完整生成（DWARF 2/3）。
mt/as 的 `.eh_frame` 生成：**单一硬编码 CIE + 所有 FDE 共享同一 CIE**，仅有最短可用的 CFI 指令集。
mt/ld 的 `.eh_frame_hdr` 生成：**已实现**（`elfout.c` `build_eh_frame_hdr`，通过 `--eh-frame-hdr` 启用）。
mt/as 的 CFI 指令解析：.cfi_startproc/.cfi_endproc + 7 种 DW_CFA_* 指令。

CI 状态：
- `check-readelf-dwarf` gate：已在 Makefile 中，CI `make -C projects/meuos-toolchain check` 会触发 ✓
- `check-as-dwarf` gate：已在 Makefile 和 CI check 列表中 ✓
- `make check-dwarf`（mcc 侧）：已在 Makefile ✓

## 缺口清单

### §1 CIE 生成（as_dwarf.c）

| # | 缺口 | 现状 | 影响 |
|---|------|------|------|
| 1.1 | **RA register 硬编码为 x86_64 (16)** | `dwarf_u8(as, eh, 16)` 写死 | i386 (RA=8 ESP)、aarch64 (RA=30 LR)、riscv64 (RA=1 RA)、loongarch64 (RA=3) 全部用错 RA 寄存器号，异常展开时崩溃 |
| 1.2 | **CIE version 固定为 1** | `dwarf_u8(as, eh, 1)` 写死 | DWARF 3 及以上版本使用 version 3/4，某些工具可能拒绝 version 1 |
| 1.3 | **augmentation "zR" 固定** | `dwarf_string(as, eh, "zR")` | 缺少 'L'（LSDA）、'P'（personality）、'S'（signal frame）扩展 |
| 1.4 | **FDE encoding 固定为 absolute (0x00)** | `dwarf_u8(as, eh, 0x00)` | 在动态链接/PIE 场景下，absolute address 无法被 ld.so 正确重定位，需切换到 pc-relative (0x1b pcrel\|sdata4) |
| 1.5 | **无 personality routine 支持** | 未实现 `.cfi_personality` | C++ 异常处理需要 `__gxx_personality_v0`，否则异常抛出后无法解析 LSDA 决定 catch 匹配 |
| 1.6 | **无 LSDA 支持** | 未实现 `.cfi_lsda` | 每个函数需要 LSDA 指针指向 call site 表（.gcc_except_table），否则无法确定哪些 catch 块匹配当前异常 |
| 1.7 | **无 SIGNAL_FRAME 支持** | 未实现 `.cfi_signal_frame` | 信号处理函数（`sigaction`）需要在 FDE 中标记 `SIGNAL_FRAME`，否则展开器跳过 |
| 1.8 | **codes_align/data_align 硬编码** | 固定为 1/1 | 不同架构不同：x86_64 通常 1/8，aarch64 4/8，riscv64 2/8 |
| 1.9 | **无多 CIE 支持** | 所有 FDE 共享一个 CIE | 不同函数可能有不同 personality/LSDA，需要不同 CIE |

### §2 FDE 生成（as_dwarf.c）

| # | 缺口 | 现状 | 影响 |
|---|------|------|------|
| 2.1 | **FDE 地址编码固定为 4 字节 absolute** | `dwarf_u32(as, eh, ...)` | 64 位代码需 8 字节地址；PIE 需 pc-relative 编码。当前仅支持 32 位地址空间 |
| 2.2 | **FDE initial_loc 无架构宽度感知** | 写死 `dwarf_u32` | x86_64/aarch64/riscv64 需 8 字节或 `DW_EH_PE_pcrel\|DW_EH_PE_sdata4` (0x1b) |
| 2.3 | **FDE func_size 无架构宽度感知** | 写死 `dwarf_u32` | 同上 |
| 2.4 | **FDE 对齐仅 4 字节** | `while (eh->size % 4 != 0)` | 64 位架构可能需要 8 字节对齐 |
| 2.5 | **CIE pointer 编码为 4 字节 absolute** | `dwarf_u32(as, eh, ...)` | 大型 .eh_frame 段（>4GB）或跨段 CIE 引用需 8 字节 |

### §3 CFI 指令集（as_parse.c）

**已支持**（7 种 DW_CFA_*）：
- `DW_CFA_def_cfa` (0x0c) — `.cfi_def_cfa reg, off`
- `DW_CFA_offset` (0x80\|reg) — `.cfi_offset reg, off`
- `DW_CFA_def_cfa_register` (0x07) — `.cfi_def_cfa_register reg`
- `DW_CFA_def_cfa_offset` (0x0e) — `.cfi_def_cfa_offset off`
- `DW_CFA_register` (0x08) — `.cfi_register reg1, reg2`
- `DW_CFA_same_value` (0x09) — `.cfi_same_value reg`
- `DW_CFA_remember_state` (0x0a) / `DW_CFA_restore_state` (0x0b)
- `DW_CFA_rel_offset` (0x80\|reg) — `.cfi_rel_offset reg, off`

**未支持**：

| # | 指令 | DW_CFA | 用途 |
|---|------|--------|------|
| 3.1 | `.cfi_personality` | — | 设置 CIE personality routine（C++ 异常处理关键） |
| 3.2 | `.cfi_lsda` | — | 设置 FDE 的 LSDA 指针 |
| 3.3 | `.cfi_sections` | — | 选择 .eh_frame 或 .debug_frame 输出 |
| 3.4 | `.cfi_signal_frame` | — | CIE 标记为信号帧 |
| 3.5 | `.cfi_window_save` | 0x2d | SPARC 窗口保存 |
| 3.6 | `DW_CFA_expression` | 0x10 | 复杂规则表达式 |
| 3.7 | `DW_CFA_val_expression` | 0x0f | 值表达式 |
| 3.8 | `.cfi_escape` | — | 原始 DW_CFA 字节序列 |
| 3.9 | `.cfi_return_column` | — | 设置 RA 寄存器列 |
| 3.10 | `.cfi_adjust_cfa_offset` | — | CFA 偏移调整 |
| 3.11 | `DW_CFA_GNU_args_size` | 0x2e | GNU 扩展 v6 参数大小（栈展开时需要） |

### §4 mt/ld .eh_frame_hdr（elfout.c）

| # | 缺口 | 现状 | 影响 |
|---|------|------|------|
| 4.1 | **eh_frame_hdr 编码固定为 sdata4** | `hdr[1] = 0x1b`（pcrel\|sdata4） | 大型二进制（>2GB 偏移）可能溢出，需要 sdata8 (0x1c) |
| 4.2 | **FDE 表项固定 8 字节** | 每个条目 4B PC + 4B FDE | 64 位二进制需 8+8 或 4+4（pcrel sdata4 可覆盖 ±2GB） |
| 4.3 | **无 mixed 编码支持** | 所有 FDE 用同一编码 | 部分 FDE 可能需 8 字节地址，另一些 4 字节 |
| 4.4 | **无 .eh_frame 合并/去重** | 简单拼接 | 多个 .o 的 .eh_frame 段直接拼接，未做 CIE 去重合并（GNU ld 会合并相同 CIE） |

### §5 架构依赖——当前仅支持 x86_64

| # | 架构 | RA # | CIE aug | alignment | 备注 |
|---|------|------|---------|-----------|------|
| 5.1 | x86_64 | 16 | "zR" | 1/1 | 当前唯一支持的架构 |
| 5.2 | i386 | 8 | "zR" | 1/4 | ESP 是返回地址，CFA = ESP+4 |
| 5.3 | aarch64 | 30 | "zR" | 4/8 | LR=x30，CFA = SP，code align 4 |
| 5.4 | riscv64 | 1 | "zR" | 2/8 | RA=x1，CFA = SP |
| 5.5 | loongarch64 | 3 | "zR" | 4/8 | RA=r3 |
| 5.6 | arm | 14 | "zR" | 2/4 | LR=r14 |

### §6 mcc 端 CFI 生成

| # | 缺口 | 现状 | 影响 |
|---|------|------|------|
| 6.1 | **mcc 不生成 .cfi_* 指令** | 无 CFI 输出 | mcc -g 只生成 `.loc` 行号信息，不生成 `.cfi_startproc/.cfi_def_cfa/.cfi_offset` 等 |
| 6.2 | **mcc 后端无栈帧展开信息** | 仅 C++ 后端注释提及 | 当前 mcc 的 C 和 C++ 后端均不发射 CFI 指令 |

## 优先修复建议

### P0（异常处理阻断）

1. **CIE 架构参数化**：`as_dwarf.c` 根据 `--target=` 选择 RA register、CIE 对齐、地址宽度
2. **FDE 编码参数化**：支持 `DW_EH_PE_pcrel \| DW_EH_PE_sdata4` (0x1b) 编码，而非固定 absolute
3. **`.cfi_personality` + `.cfi_lsda` 解析**：`as_parse.c` 新增这两个 directive → 注入 CIE/FDE augmentation

### P1（mcc 端）

4. **mcc 后端发射 CFI**：在函数序言/尾声发射 `.cfi_startproc`/`.cfi_endproc`/`.cfi_def_cfa`/`.cfi_offset` 指令
5. **mcc -g 与异常处理集成**：LSDA 表（.gcc_except_table）生成

### P2（mt/ld 端）

6. **.eh_frame CIE 合并**：mt/ld 链接时去重合并相同 CIE 条目
7. **.eh_frame_hdr 64 位编码**：支持 sdata8 编码

## 验收标准

### 短期（P0 修复后）

- `mt/as --target=x86_64 -o t.o t.s` 含 `.cfi_startproc`/`.cfi_def_cfa`/`.cfi_offset` 的汇编 → 生成的 `.eh_frame` 被 `mt/readelf -w` 正确解码
- `mt/as --target=i386` 生成的 CIE RA register=8
- `mt/ld --eh-frame-hdr` 生成的 `.eh_frame_hdr` 被 `mt/readelf -w` 正确解码

### 长期（P1+P2 修复后）

- `mcc -g test.c` → `.cfi_*` 指令 → mt/as 编码 → mt/ld 链接 → `mt/readelf -w` 解码成功
- C++ 异常（`throw`/`catch`）通过 `.eh_frame` 展开栈帧运行正常
- 所有架构 `make check` 通过
- 自举验证通过（`check-sysroot-static`）