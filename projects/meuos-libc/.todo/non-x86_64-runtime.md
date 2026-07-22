# 待实现：非 x86_64 完整 runtime 验证

## 背景
STATE.md §3（已弃用；按 AGENTS.md §2 各子项目独立维护状态）：完整独立 MeuOS userspace
与纯原生链接器仍未完成；非 x86_64 runtime 端到端验证是其中一道门禁。
x86_64 与 aarch64 是当前已端到端 qemu 验证的目标（见 §进度 aarch64 行）；riscv64 /
loongarch64 仍在移植路线中。

## 目标
为至少一个 64 位非宿主 target（推荐 aarch64 或 riscv64）补齐 crt1、原子
runtime、syscall gate 与 meuos-libc，并增加 qemu 运行测试，使其达到与
x86_64 等价的回归覆盖。

## 影响范围
- `src/arch/<arch>/` 全套（见对应 .todo）。
- `src/internal/syscall.h` + `include/sys/syscall.h` 的 syscall 编号。
- `Makefile` 的 `ARCH=<arch>` 规则。
- `test/` 增加 qemu 运行回归。

## 验收
- `make ARCH=<arch>` 全量编译通过。
- qemu 下 atomic/threads/TLS/stdio/process 回归全通过。
- 该 target 的 Phase-2 测试程序输出 `counter = 2000`。

## 进度

### aarch64 — ✅ 已完成（work/aarch64-runtime-cont）

- crt1.S / syscall.S / atomic.S / setjmp.S / sigreturn.S / thread_clone.S / set_tls.S / tls.c 全套存在；
  Makefile 已经注册 `ARCH=aarch64`，`arch_prctl.o` 在该 arch build 中按 ifeq 过滤。
- syscall.h aarch64 翻译表覆盖 read/write/open/close/mmap/munmap/brk/ioctl/clone/exit/exit_group/
  wait4/getpid/gettid/futex/getdents64/clock_gettime/nanosleep/socket/bind/listen/connect/kill/
  fcntl/flock/fchmod/fchown/umask/gettimeofday/times/getuid/getgid/geteuid/getegid/rt_sigaction/
  rt_sigprocmask/rt_sigpending/rt_sigsuspend/sigaltstack/tgkill + *at 变体 (openat/mkdirat/fchownat/
  newfstatat/unlinkat/renameat/linkat/symlinkat/readlinkat/fchmodat/faccessat/dup3/pipe2/statx/
  utimensat)。
- `make ARCH=aarch64` 全量编译通过；产物 `crt1.o`、`libc-meuos.a`、`libatomic-meuos.a` 均为
  ELF64/AArch64。
- `test/aarch64-bootstrap.sh` 提供跨编译自检（默认）和可选 qemu-aarch64-static 运行时 gate
  （`MEUOS_AARCH64_RUN=1`）。当前验证基线（qemu-user-static v7.2.0-1）：

  | 测试 | 期望输出 |
  |---|---|
  | hello | "aarch64 MeuOS libc" |
  | atomic-test (int/uchar/ushort) | exit 0 |
  | phase2_counter (2 线程 x1000) | "counter = 2000" |
  | bare_tls (_Thread_local + errno 隔离) | "tls main=5 child=9 errno=31/47" |

- **关键 TLS 布局修复**：`aarch64 GAP_ABOVE_TP = 16`，静态链接器把该 GAP 烤进 R_AARCH64_TLSLE_*
  reloc 的 addend，因此 TPIDR_EL0 必须指向 mmap 起点，`.tdata` 必须 memcpy 到 `mmap + 16`。
  修复见 `src/arch/aarch64/tls.c` 的 `MEUOS_TLS_GAP` 与 `allocate_tls()`；未修复前主线程
  `_Thread_local` 初值读到 0。
- **x86_64 专有 syscall 隔离**：`Makefile` 用 `ifeq ($(ARCH),x86_64)` 把 `arch_prctl.o` 收进
  `ARCH_PRCTL_OBJ`，避免给 i386/aarch64 build 列表里塞一条无意义的目标。

### riscv64 / loongarch64 — 未开始

参考 aarch64 的工作流（crt1 + set_tls + setjmp + sigreturn + thread_clone + tls.c + syscall
翻译表 + qemu 运行时 gate）。
