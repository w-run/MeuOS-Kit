<!--
priority: P1
status: done
done_ts: 2026-07-23
note: x86_64 / aarch64 / i386 / riscv64 / loongarch64 5 架构端到端门禁
-->

# 非 x86_64 完整 runtime 验证 — 5 架构全部完成

> Update 2026-07-23：x86_64（基线）、aarch64、i386、riscv64、loongarch64
> 5 架构 runtime + 端到端 qemu 门禁全部就位。本文件保留为多架构移植
> 历史参考,作为新增架构（如 armv7 / powerpc64le / s390x）的模板。

## 状态总览

| 架构 | runtime | qemu 门禁 | bootstrap 脚本 | 状态 |
|------|:-------:|:---------:|:--------------:|:----:|
| x86_64  | ✅ 完整 | ✅ 宿主 | — | ✅ 基线 |
| aarch64 | ✅ 完整 | ✅ qemu-aarch64-static | `aarch64-bootstrap.sh` | ✅ |
| i386    | ✅ 完整 | ✅ qemu-i386-static    | `i386-bootstrap.sh`    | ✅ |
| riscv64 | ✅ 完整 | ✅ qemu-riscv64-static | `riscv64-bootstrap.sh` | ✅ |
| loongarch64 | ✅ 完整 | ✅ qemu-loongarch64-static | `loongarch64-bootstrap.sh` | ✅ |
| armv7   | ❌ | ❌ | — | ⬜ 占位 |
| powerpc64le | ❌ | ❌ | — | ⬜ 占位 |
| s390x   | ❌ | ❌ | — | ⬜ 占位 |

## 各架构关键节点

### aarch64 — ✅（main，2a3f5c9 / 25f9384）
- 全套 crt1/syscall/atomic/setjmp/sigreturn/thread_clone/set_tls/tls.c
- aarch64 `GAP_ABOVE_TP = 16`, 静态链接器把 GAP 烤进 R_AARCH64_TLSLE_*
  reloc addend; TPIDR_EL0 指向 mmap 起点, .tdata memcpy 到 `mmap+16`
- `make ARCH=aarch64` + `test/aarch64-bootstrap.sh` 端到端通过
  (hello / atomic-test / phase2_counter=2000 / bare_tls main=5 child=9)

### i386 — ✅
- 全套 arch/i386: atomic.S / load_gs.S / setjmp.S / sigreturn.S /
  soft_arith.c / thread_clone.S / tls.c + internal/arch/i386/syscall.S
- time64 基石: `time_t=int64_t` / `statx(383)` / `mmap2(192)`
- socketcall(102) 多路复用
- 跨函数 va_list 已修复 (mcc targ.c typevalist 改 struct)
- QEMU 端到端 gate: `make check-i386-qemu` 通过 hello/atomic/setjmp/
  phase2_counter; runtime_kl/runtime_fp/runtime_time64/runtime_va/
  fp_unsigned/fp_arith 全量
- 64 位乘除/取余: pre-pass 重写为 libc 软算术调用

### riscv64 — ✅（eaf8803）
- 全套 arch/riscv64: atomic.S / setjmp.S / sigreturn.S /
  thread_clone.S / tls.c
- `make ARCH=riscv64` + `test/riscv64-bootstrap.sh` 通过

### loongarch64 — ✅（a03a41f）
- 全套 arch/loongarch64: atomic.S / setjmp.S / sigreturn.S /
  thread_clone.S / tls.c
- 修复 riscv64 21 个 wrapper 条件分支遗留
- `make ARCH=loongarch64` + `test/loongarch64-bootstrap.sh` 通过

## 关键复用模式（供 armv7 / powerpc64le / s390x 参考）

```
arch/<arch>/
├── atomic.S          # __atomic_* / a_*_l 系列
├── setjmp.S          # setjmp/longjmp
├── sigreturn.S       # rt_sigreturn
├── thread_clone.S    # clone
├── tls.c             # set_tls / get_tls / allocate_tls
└── (arch-specific)   # 例 i386: load_gs.S, soft_arith.c
internal/arch/<arch>/
└── syscall.S         # int $0x80 / svc / ecall / syscall 入口
crt/<arch>/
└── crt1.S            # _start → __libc_start_main
test/<arch>-bootstrap.sh   # 跨编译自检 + qemu 运行 gate
```

Makefile 关键点:
- `ARCH ?= x86_64` + `ifeq ($(ARCH),x86_64) ARCH_PRCTL_OBJ := ...`
  隔离宿主专有 syscall wrapper
- `ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o ...` 多 arch 通用模板

## 后续

- 新增 armv7 / powerpc64le / s390x 时,按上述模板填空即可
- GD-TLS（见 [mcc/.todo/gd-tls.md](../mcc/.todo/gd-tls.md)）就位后,
  各架构 TLS 模型选择可从 LE-only 扩展为 LE+IE+GD

## 验收标准

<!-- TODO(main session): fill in concrete commands. -->

```
make -C projects/meuos-libc check
```

