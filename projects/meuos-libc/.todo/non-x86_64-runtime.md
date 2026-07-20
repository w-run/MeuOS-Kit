# 待实现：非 x86_64 完整 runtime 验证

## 背景
STATE.md §3：完整独立 MeuOS userspace、非 x86_64 runtime 与纯原生链接器
仍未完成，因此不将完整 Phase 2 标记为完成。当前 x86_64 是唯一已运行验证目标。

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
