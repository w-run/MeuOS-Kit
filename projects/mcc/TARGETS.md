# mcc Target Status

本表区分“能选择 target”与“可作为完整 MeuOS 用户空间工具链”。后者还需要
对应 ABI 的 `crt1`、`meuos-libc`、链接器路径以及运行时验证。

| Target | 整数 ABI 回归 | 浮点汇编 | 当前结论 |
|---|---:|---:|---|
| x86_64 | 宿主运行验证 | 支持 | Phase 1/2 的主开发目标 |
| aarch64 | **qemu 端到端** | 支持 | libc runtime 完整（crt1/atomic/setjmp/sigreturn/thread_clone/tls + syscall gate + *at 翻译表）；qemu-aarch64-static hello/atomic/phase2/bare_tls 全绿；TLS GAP_ABOVE_TP=16 + mcc store fix 已固化 |
| riscv64 | 汇编回归 | 支持 | 代码生成基线可用；libc runtime 已落地（crt1/atomic/setjmp/sigreturn/thread_clone/tls + syscall gate）；qemu 运行时门禁待交叉工具链就绪 |
| loongarch64 | 专项 ABI/VLA/TLS 汇编回归 | 支持 | 后端相对成熟；libc runtime 已落地（crt1/atomic/setjmp/sigreturn/thread_clone/tls + syscall gate）；qemu 运行时门禁待交叉工具链就绪 |
| arm | ARM 汇编回归 | 支持 | ARMv7 后端完成，qemu 运行时验证通过 |
| i386 | 整数+浮点 SysV ABI 回归 | 支持 | x87 浮点完整（float 返回/二元运算/signed·unsigned 浮点↔整数转换）+ Ouwtof/Oultof + 跨函数 va_list 均通过回归；FPR 占位已收口（设计决策：x87 栈机不建模为 flat FPR 类，刻意 `nfpr=0`）；**完整代码生成 target**：端到端 runtime 数值验证已通过（bootstrap-verify）。双路径：(1) 宿主内核 `CONFIG_IA32_EMULATION` 直接执行静态 32 位 ELF；(2) **env qemu-system-i386 真实 32 位内核门禁**（`make check-i386-qemu` / `test/i386/qemu-runtime.sh`，Alpine 6.6.x + TCG，经 9p 共享编译+运行+回传） |

## 当前回归

```sh
make -C mcc check-i386
make -C mcc check-targets       # aarch64 + riscv64
make -C mcc check-loongarch64
make -C mcc check-driver        # --shared、TLS 地址与浮点比较
make -C meuos-libc check-aarch64-bootstrap
# 默认只交叉编译并验证 ELF64/AArch64 头；设 MEUOS_AARCH64_RUN=1 且
# MEUOS_AARCH64_QEMU=<qemu-aarch64-static 路径> 时附加 qemu 运行时 gate
# （期望 hello="aarch64 MeuOS libc", phase2="counter = 2000",
# bare_tls="tls main=5 child=9 errno=31/47"）。
```

`check-targets` 与 `check-i386` 是交叉汇编生成测试；它们不替代目标机执行。
`check-aarch64-bootstrap` 是 meuos-libc 的跨架构 gate——既验证 mcc 的 aarch64
代码生成 + store fix，也通过 qemu-user-static 验证 libc runtime（C11 原子、
TLS 布局、线程/futex、stdio）端到端可用。

## TLS 与共享库边界

`x86_64`、`aarch64`、`riscv64` 和 `loongarch64` 都有 local-exec TLS 地址
生成回归；x86_64 也覆盖了外部 TLS 的 initial-exec 地址模型。`--shared`
已经通过宿主链接器生成普通 ELF DSO 的回归，x86_64 的本地 `_Thread_local`
定义会自动改用可链接的 initial-exec GOT 地址模型。当前尚未实现
general-dynamic TLS；aarch64、riscv64、loongarch64 的外部 TLS 地址，以及这些
target 含 TLS 的 DSO，仍不在支持范围内。

## 后续优先级

1. ✅ i386 完整代码生成 target（2026-07-22 全部完成）：
   - as-x86_64 跨函数 va_list（typevalist 改 struct）、mcc-mt-integrate FPR 收口（fpr0=0, nfpr=0）、
     浮点 emit 四类缺陷修复（float_binary float_ref、Ostosi/Odtosi Kl 目标、
     Ostoui/Odtoui 新增实现、Ouwtof/Oultof 新增实现）均已完成。
   - `make check-i386` + `make check-i386-qemu` 双门禁全绿，i386 为完整 target。
2. ✅ aarch64：libc runtime + qemu gate 端到端通过；下一个目标是 riscv64，
   借助 aarch64 的工作流（crt1 + set_tls + setjmp + sigreturn + thread_clone
   + tls.c + syscall 翻译表 + qemu 运行时 gate）复制。
3. 对 LoongArch64 按相同路径补齐 libc/runtime；现有后端专项回归可作为基线。
4. i386 剩余项：TLS/信号上下文端到端验证。
5. ARMv7、powerpc64le、s390x 等只在有明确 MeuOS 平台需求时再引入，避免在缺少
   sysroot 和运行验证时只增加未维护的后端。
