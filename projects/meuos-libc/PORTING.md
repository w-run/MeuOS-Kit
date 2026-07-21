# meuos-libc 多架构移植说明

> 更新：2026-07-21
>
> 本文记录 `meuos-libc` 的架构取舍、移植边界与验收顺序。它是移植路线的
> 动态说明；代码当前是否已经落地，以仓库中的源码、`.todo/` 和 `STATE.md`
> 为准。

## 1. 状态口径

本文中的“已确认基石”表示该架构已经被选为长期支持和 ABI 设计基准，**不表示
该架构的 `meuos-libc` runtime 已经完成**。例如 aarch64、loongarch64 目前是
mcc 已有代码生成基线、libc 运行时仍待移植；x86_64 才是当前完整运行验证基线。

| 架构            | 战略状态         | 当前仓库事实                                                                      | 移植定位与下一道门禁                                                                   |
| --------------- | ---------------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **x86_64**      | **已确认基石**   | libc runtime 完整；`counter = 2000`、线程、TLS、stdio、静态链接和 QEMU 回归已通过 | 主开发平台、参考实现和每次变更的回归基线                                               |
| **aarch64**     | **已确认基石**   | mcc 后端有基线；`crt/`、`src/arch/`、syscall gate 仍是待实现项                    | 第一优先级的 64 位次级基石；完成 Linux ABI、TLS、原子、线程后接入 QEMU                 |
| **loongarch64** | **已确认基石**   | mcc 后端有 ABI/汇编回归基线；libc runtime 尚未落地                                | 龙芯新生态；syscall 编号、ABI、TLS 和内核接口必须按最新 Linux UAPI 校准                |
| **i386**        | **已确认基石**   | 有可运行的整数 ABI bootstrap；浮点、跨函数 `va_list` 等仍有限制                   | 32 位兼容基石；`time_t` 必须改为 64 位，并补齐 time64 syscall/API，彻底消除 2038 问题  |
| **riscv64**     | **强烈建议新增** | mcc 后端有基线；libc runtime 尚未落地                                             | 无历史包袱的 64 位架构，用于检验 syscall/TLS/原子等抽象是否干净                        |
| **armv7**       | **强烈建议新增** | 当前只有占位 TODO，没有 libc runtime 或构建目标                                   | ARMv7 hard-float EABI；同时验证 32 位指针、VFP/硬浮点 ABI、原子、TLS 与 64 位 `time_t` |
| **ppc64le**     | **按需可选**     | 当前未实现、未纳入默认构建                                                        | POWER 服务器目标；在基础多架构 runtime 稳定后再投入，重点验证多寄存器调用约定          |
| **s390x**       | **按需可选**     | 当前未实现、未纳入默认构建                                                        | IBM 大型机目标；重点验证独特 syscall 机制、寄存器约定和信号上下文                      |
| **armel**       | **明确跳过**     | 不创建实现目标                                                                    | 极老软浮点 ARM；armv7 已覆盖仍有价值的 32 位 ARM 场景                                  |
| `mips*`         | **明确跳过**     | 不创建实现目标                                                                    | 生态持续萎缩；龙芯路线已转向 LoongArch，不投入平移成本                                 |

## 2. 移植边界

### 2.1 保持架构无关的部分

以下模块原则上使用纯 C 实现，不应因为目标架构而复制一套逻辑：

- `src/string/`、`src/stdio/`、`src/stdlib/` 的算法和格式化核心；
- `src/dirent/`、`src/ctype/`、`src/errno/` 的公共接口；
- C11 线程的状态机、锁、条件变量和 TSS 的高层实现；
- syscall wrapper 的参数检查、负 errno 转换和 POSIX 语义。

如果这些模块需要架构条件分支，优先把差异收敛到公共的内部 ABI 头，而不是在
每个调用点散落 `#ifdef`。

### 2.2 每个架构必须提供的部分

每个纳入实现的架构都应有同一组目录和职责：

```text
crt/<arch>/crt1.S
src/internal/arch/<arch>/syscall.S
src/arch/<arch>/atomic.S
src/arch/<arch>/setjmp.S
src/arch/<arch>/sigreturn.S
src/arch/<arch>/thread_clone.S
src/arch/<arch>/tls.c       # 或等价的最小汇编实现
```

并在 `Makefile`、`include/sys/syscall.h`、`src/internal/syscall.h` 和测试夹具中
注册该架构。没有上述完整集合时，只能标记为 bootstrap/汇编回归目标，不能标记为
“完整 libc”。

每个架构的实现职责固定为：

1. **数据模型和 ABI**：`sizeof`、对齐、栈对齐、参数/返回值分类、结构体布局；
2. **syscall gate**：将 C 调用约定转换为 Linux syscall 调用约定，使用该架构自己的
   syscall 编号表；
3. **启动与退出**：从初始栈解析 `argc/argv/envp/auxv`，设置 ABI 要求的栈对齐，
   初始化 TLS，调用 `main`，最后直接进入 `exit` syscall；
4. **C11 原子**：1/2/4/8 字节的 load/store/exchange/CAS/fetch-op 及
   acquire/release/seq_cst 屏障；
5. **TLS 和线程**：线程指针、`clone` 参数、子线程入口、futex 等待/唤醒，以及
   `pthread`/`threads.h` 所需的最低 ABI；
6. **非局部控制流和信号**：`setjmp/longjmp` 保存调用者保存寄存器，提供
   `rt_sigreturn` restorer，并确保 `sigaction` 上下文布局与内核一致。

## 3. ABI 与时间策略

### 3.1 类型宽度不可由宿主推断

`meuos-libc` 面向目标 Linux ABI 编译，不能通过宿主的 `<sys/types.h>` 或宿主
libc 类型定义“碰巧工作”。至少要为每个目标明确记录：

- `size_t`、`ptrdiff_t`、`uintptr_t`、`long` 和指针宽度；
- `off_t`、`ino_t`、`dev_t`、`blksize_t` 的宽度和结构体布局；
- `time_t`、`suseconds_t`、`struct timespec/timeval` 的布局；
- `max_align_t`、原子类型的自然对齐和锁的对齐。

### 3.2 32 位目标统一采用 time64

i386 和新增的 armv7 不沿用 32 位 `long time_t`。公共头和实现应统一采用 64 位
有符号 `time_t`，并使用 Linux 的 time64 syscall（或等价的内核接口）实现
`clock_gettime`、`nanosleep`、`futex` 等涉及时间的调用。旧 syscall 只可作为
明确兼容旧内核的适配路径，不能让公共 API 退回 32 位秒数。

这项策略需要同时修改类型定义、syscall 参数打包、`stat`/`timeval` 等 ABI 结构、
测试程序和跨架构 QEMU 门禁；只改 `typedef` 而不改 syscall 入口是不完整的。

### 3.3 syscall 规则

- syscall 编号来自目标架构对应的 Linux UAPI；不得复用 x86_64 编号作为最终 ABI；
- 64 位参数在 32 位架构上的寄存器拆分、对齐槽位和返回值拼接必须显式处理；
- 内核返回 `[-4095, -1]` 范围的错误码由统一 gate 转换为 `errno`，公共 wrapper
  不通过宿主 libc；
- `socketcall`、`mmap2`、旧版 `stat` 等架构特有接口只能在对应 target 的内部
  适配层出现。

## 4. 各基石的实现要点

### x86_64

以现有实现为规范：SysV AMD64 调用约定、Linux syscall `rax/rdi/rsi/rdx/r10/r8/r9`、
FS 线程指针、`lock` 原子指令、16 字节栈对齐。任何新架构功能先在这里补齐
可移植测试，再移植到其他 target。

### aarch64

使用 AAPCS64，syscall 号放入 `x8`、参数放入 `x0`–`x5`、通过 `svc #0` 进入内核；
线程指针使用 `TPIDR_EL0`。原子优先用 `ldxr/stxr`（或按 CPU 能力选择 LSE），
不能假设宿主编译器会替 libc 生成完整 C11 原子 runtime。需要独立实现
`crt1`、`setjmp`、`sigreturn` 和 clone 子线程入口。

### loongarch64

使用 Linux LoongArch LP64D/LP64S 对应的目标 ABI，syscall、TLS（`$tp`）、
原子 ll/sc 序列、信号上下文和浮点 ABI 必须以当前内核 UAPI 与 ABI 文档为准；
不要从 MIPS 复制 syscall 或寄存器假设。先完成整数/无浮点依赖的 Phase-2
闭环，再补齐浮点和完整线程回归。

### i386

使用 Linux i386 `int $0x80` gate 和 `mmap2` 等 32 位接口。当前 bootstrap 只覆盖
整数 ABI；完整目标还必须解决：64 位 `time_t`/time64、64 位 syscall 参数打包、
浮点 ABI、跨函数 `va_list`、TLS 与信号上下文。强制 64 位 `time_t` 是公共 ABI
政策，不得用 `-D_TIME_BITS=64` 这种仅对某个构建命令生效的旁路替代。

### riscv64

使用 RV64 ABI，syscall 号在 `a7`、参数在 `a0`–`a5`、通过 `ecall` 进入内核；
线程指针为 `tp`。原子使用 `lr/sc` 或目标扩展允许时的原子指令，`setjmp` 至少
保存 `ra/sp/s0`–`s11` 和必要的浮点状态。它是验证“架构差异只存在于边界层”的
优先新目标。

### armv7

使用 ARMv7 hard-float EABI，明确区分整数寄存器参数与 VFP 参数，处理 8 字节参数
对齐和结构体返回规则；线程指针、`clone`、原子和信号上下文均不能照搬 i386。
公共类型坚持 64 位 `time_t`，并增加整数-only libc 代码与 VFP/硬浮点调用约定的
编译和运行测试，避免把 `armv7` 错误降级成 armel。

## 5. 推荐移植顺序与验收

1. **x86_64 保持绿**：所有公共行为先通过现有 `make -C projects/meuos-libc check`。
2. **i386 收口**：先修复跨函数 `va_list`，再完成 time64 类型和 syscall 适配，补
   2038 边界测试；在没有浮点完整支持前仍标记为 bootstrap/整数 ABI。
3. **aarch64**：完成全套 arch runtime、构建注册和 QEMU Phase-2 回归，作为第一条
   64 位跨 ISA 完整链。
4. **loongarch64**：按最新 ABI/UAPI 完成同样的门禁，独立验证 syscall 编号、TLS、
   原子和信号布局。
5. **riscv64**：借助较干净的 ABI 抽象完成第三条 64 位完整链，反向清理公共层的
   架构泄漏。
6. **armv7**：在 64 位目标稳定后加入，重点验证 32/64 位变体和 hard-float；先以
   QEMU + 交叉汇编器为门禁，不影响默认 x86_64 构建。
7. **ppc64le/s390x**：仅在明确用户空间需求或有可用 QEMU/硬件门禁时排期。
8. **armel/mips\***：保持排除清单，不创建默认 target、不在核心库引入兼容性分支。

每个新 target 的最小验收必须同时包括：

- `make ARCH=<arch>` 产出 `crt1.o`、`libc-meuos.a`、`libatomic-meuos.a`；
- 无宿主 libc 的静态程序能运行 `write`、`malloc`、stdio、C11 atomic；
- TLS、`thrd_create/thrd_join`、futex、signal/setjmp 回归；
- `time_t` 宽度和 2038 边界（32 位目标）；
- 通过对应 QEMU VM 或真实硬件运行，而不只检查汇编文件能否生成；
- 目标源码中不出现 glibc 私有头、LLVM/GCC 代码或宿主 ABI 假设。
