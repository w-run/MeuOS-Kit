# mt DWARF .eh_frame（异常处理展开信息）完整化缺口清单

> 状态：🔄 部分闭环（2026-08-07 复查）
> 范围：mt/as（`as_dwarf.c` + `as_parse.c`）+ mt/ld（`elfout.c` .eh_frame_hdr）
> 关联：mcc 异常处理（C++ 异常、setjmp/longjmp 回溯 unwind）

## 当前状态（2026-08-07）

mcc-dev 领先 origin/main 55 commit 后，P0 缺口已全部闭环：

| § | 缺口 | 状态 | 闭环 commit |
|---|------|------|-------------|
| 1.1 | RA register 硬编码 x86_64 | ✅ | `42cdae7a` + `0abe982a` |
| 1.2 | CIE version 固定 1 | ✅ | `42cdae7a` |
| 1.3 | augmentation "zR" 固定 | ✅ | `d7ab1beb` |
| 1.4 | FDE encoding 固定 absolute | ✅ | `42cdae7a` |
| 1.5 | 无 personality routine | ✅ | `42cdae7a`（.cfi_personality） |
| 1.6 | 无 LSDA 支持 | ✅ | `42cdae7a`（.cfi_lsda） |
| 1.7 | 无 SIGNAL_FRAME 支持 | ✅ | `d7ab1beb`（.cfi_signal_frame） |
| 1.8 | codes_align/data_align 硬编码 | ✅ | `42cdae7a`（从 mt_target 读取） |
| 1.9 | 无多 CIE 支持 | ✅ | `d7ab1beb`（multi-CIE 分组） |
| 2.1 | FDE 地址编码 | ✅ | `42cdae7a`（使用 dwarf_fde_encoding） |
| 2.4 | FDE 对齐 | ✅ | `42cdae7a`（按 elf_class 对齐） |
| 3.1-3.11 | CFI 指令集 | ✅ | `42cdae7a`, `d7ab1beb`, `f236e999` |
| 4.1 | .eh_frame_hdr 编码 | ✅ | `e6cdb566` |
| 5.1-5.6 | 架构依赖 | ✅ | 6 架构参数化完成 |

## 回归门

- `check-as-dwarf-eh`：6 架构 .eh_frame 被 host readelf 正确解析
- `check-as-dwarf`：mt/as DWARF 伪指令
- `check-readelf-dwarf`：mt/readelf DWARF 解码
- `check-ld-pie`：PIE + ld.so 全链

## 剩余开放项

| # | 项 | 影响 | 优先级 |
|---|-----|------|--------|
| 6.1 | mcc -g 不生成 .cfi_* 指令 | mcc 编译 C 不产 CFI | P1（mcc 端） |
| 6.2 | mcc 后端无栈帧展开信息 | C/C++ 异常展开需 CFI | P1（mcc 端） |
| 4.3 | 无 mixed 编码 | %s/| 大型二进制 | P2 |
| 4.4 | 无 .eh_frame CIE 去重合并 | 冗余 CIE | P2（mt/ld 端） |
