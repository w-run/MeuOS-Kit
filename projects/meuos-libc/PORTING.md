# meuos-libc 多架构移植说明

> 更新：2026-07-27
>
> 本文记录 `meuos-libc` 的架构取舍、移植边界与验收顺序。它是移植路线的
> 动态说明；代码当前是否已经落地，以仓库中的源码、`.todo/` 和 `STATE.md`
> 为准。

## 1. 状态口径

本文中的“已确认基石”表示该架构已经被选为长期支持和 ABI 设计基准，**不表示
该架构的 `meuos-libc` runtime 已经完成**。例如 loongarch64 目前是
mcc 已有代码生成基线、libc 运行时仍待移植；x86_64 和 aarch64 才是当前完整运行验证基线。

| 架构            | 战略状态         | 当前仓库事实                                                                      | 移植定位与下一道门禁                                                                   |
| --------------- | ---------------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **x86_64**      | **已确认基石**   | libc runtime 完整；`counter = 2000`、线程、TLS、stdio、静态链接和 QEMU 回归已通过 | 主开发平台、参考实现和每次变更的回归基线                                               |
| **aarch64**     | **运行验证**     | libc runtime 完整（crt1/syscall/atomic/setjmp/sigreturn/thread_clone/set_tls/tls.c）；`make ARCH=aarch64` + `test/aarch64-bootstrap.sh` qemu gate 通过 | 64 位次级基石；hello/atomic/phase2/bare_tls 端到端输出已固化，详见 §5 |
| **loongarch64** | **已落地代码**   | mcc 后端有 ABI/汇编回归基线；libc runtime 已实现（crt1/syscall/atomic/setjmp/sigreturn/thread_clone/tls.c，移植自 riscv64）；`make ARCH=loongarch64` 与 `test/loongarch64-bootstrap.sh` 已注册 | 龙芯新生态；syscall 编号、ABI、TLS 和内核接口按最新 Linux UAPI 校准；移植自 riscv64 模板，随带补全了 riscv64 遗漏的 wrapper 条件分支 |
| **i386**        | **运行验证**     | x87 浮点完整（运算/比较/signed·unsigned 转换/ABI）、FPR 收口（nfpr=0）、跨函数 `va_list` 已修复；`make check-i386` + `make check-i386-qemu` 双门禁全绿 | 32 位兼容基石；`time_t`=int64_t、statx(383)、mmap2(192)、socketcall(102) 均就位；Kl 软算术库已完成（soft_arith.c）、TLS/信号端到端（bare_tls 被 mcc i386 TLS 模型缺口阻塞，见 `mcc/.todo/gd-tls.md`） |
| **riscv64**     | **已落地代码**   | libc runtime 已实现（crt1/syscall/atomic/setjmp/sigreturn/thread_clone/tls.c）；`make ARCH=riscv64` 与 `test/riscv64-bootstrap.sh` 已注册 | 无历史包袱的 64 位架构，用于检验 syscall/TLS/原子等抽象是否干净；代码经静态核对，真机门禁待交叉工具链/qemu 就绪 |
| **arm**         | **运行验证**     | libc runtime 完整（crt1/syscall/atomic/setjmp/sigreturn/thread_clone/set_tls/tls.c 含 AEABI 兼容层）；`make ARCH=arm` 已注册；qemu-arm 运行时验证通过（hello/atomic/setjmp/exit=42） | ARMv7 hard-float EABI；同时验证 32 位指针、VFP/硬浮点 ABI、原子、TLS 与 64 位 `time_t` |
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

i386 和 arm 不沿用 32 位 `long time_t`。公共头和实现应统一采用 64 位
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
线程指针使用 `TPIDR_EL0`。原子用 `ldaxr/stlxr`（独占 monitor）实现 C11 原子
runtime，独立汇编 `crt1`、`setjmp`、`sigreturn` 和 clone 子线程入口；变体 I
TLS 由 `msr tpidr_el0` 设置，CLONE_SETTLS 直接交给 kernel。

**aarch64 TLS 布局（GAP_ABOVE_TP = 16）**：与 musl ABI 一致，静态链接器把
16 字节的 TCB 间隔烤进 `R_AARCH64_TLSLE_*` reloc 的 addend。运行时：
`TPIDR_EL0 = mmap 起点`，`.tdata` 必须 `memcpy` 到 `mmap + 16`，`__meuos_tls_alloc`
返回 `mmap 起点` 作为 `CLONE_SETTLS` 的目标值；`__meuos_tls_free` 释放整个 mmap 块。
实现见 `src/arch/aarch64/tls.c` 的 `MEUOS_TLS_GAP` 与 `allocate_tls()`。

### loongarch64

使用 Linux LoongArch LP64D ABI（硬浮点），syscall 用 `syscall 0` 指令，
编号在 `$a7`、参数 `$a0–$a5`、返回值 `$a0`；线程指针为 `$tp`（r21，
用户态可写 GPR），主线程 tp 在 `crt1.S` 用 `move $tp, $a0` 设置，子线程
经 `CLONE_SETTLS` 由内核设置（无 arch_prctl 等价 syscall）。
原子使用 `ll.w/sc.w + ll.d/sc.d + dbar 0`（全屏障），子字节(1/2)原子用
字级 `ll.w/sc.w` 掩码法；`setjmp` 保存 `$s0-$s8/$fp/$sp/$ra` 与
`$fs0-$fs7`（共 23 字，`jmp_buf[23]`；`sigjmp_buf[25]`）。
TLS 用 variant I 且 `GAP_ABOVE_TP = 0`（musl loongarch64 ABI，与 riscv64
相同）：tp 直接指向 TLS 镜像起点，`.tdata` 复制到 mmap 基址 +0。

syscall 编号：LoongArch64 采用 asm-generic 编号（与 aarch64/riscv64 完全
相同），因此 syscall.h 的翻译表通过 `|| defined(__loongarch64)` 合流到
asm-generic 分支（零重复）。运行时移植自 riscv64 模板（适配寄存器名/指令）。

当前 loongarch64 e2e 测试已完成（exit=42 / printf / phase2 counter / bare_tls）
- mcc loongarch64 后端 emit 语法不兼容问题：commit 4e99492 已修复（指令格式、寄存器命名、内存语法）
- 浮点有序/无序比较崩溃：commit 8063fbf 已修复
- riscv64/loongarch64 SExtThr (Initial-Exec TLS): 已实现

### i386

使用 Linux i386 `int $0x80` gate 和 `mmap2` 等 32 位接口。已完成项：
- 整数 ABI 完整（add/sub/neg/and/or/xor 的 Kl 分解 + shldl/shrdl）
- 64 位 `time_t`/time64：types 固定 `int64_t`、`statx(383)` 时间戳、`mmap2(192)` 就位
- socketcall(102) 多路复用（`src/syscall/socketcall.c`）
- x87 浮点完整：运算/比较/signed·unsigned 转换/ABI（FPR 收口 nfpr=0）
- 跨函数 `va_list` 已修复（mcc `typevalist` 改 4 字节 struct）
- 端到端双门禁：`runtime.sh`（宿主 ia32）+ `qemu-runtime.sh`（真 32 位内核）
  全套 runtime_kl/runtime_fp/runtime_time64/runtime_va/fp_unsigned/fp_arith 全绿
（Kl mul/div/rem 已通过 sysv 预扫描+软算术库实现；`src/arch/i386/soft_arith.c` 提供 8 个完整函数）

剩余：TLS/信号上下文端到端验证（bare_tls 被 mcc i386 后端 TLS 模型选择缺口阻塞，见 `projects/mcc/.todo/gd-tls.md` Phase A）。

强制 64 位 `time_t` 是公共 ABI 政策，不得用 `-D_TIME_BITS=64` 这种仅对某个构建命令生效的旁路替代。

### riscv64

使用 RV64 ABI（LP64D 硬浮点），syscall 号在 `a7`、参数在 `a0`–`a5`、通过
`ecall` 进入内核；线程指针为 `tp`（x4），主线程 tp 在 `crt1.S` 用 `mv tp, a0`
设置，子线程经 `CLONE_SETTLS` 由内核设置（无 arch_prctl 等价 syscall）。
原子使用 `lr.w/lr.d` + `sc.w/sc.d`（`.aqrl`）实现 C11 原子 runtime，`setjmp`
保存 `s0`–`s11`/`sp`/`ra` 与 `fs0`–`fs11`（共 26 字，`jmp_buf[26]`）。
TLS 用 variant I 且 `GAP_ABOVE_TP = 0`（musl riscv64 ABI）：tp 直接指向 TLS
镜像起点，`.tdata` 复制到 mmap 基址 +0。它是验证“架构差异只存在于边界层”的
优先新目标，runtime 代码已落地，移植自 aarch64 模板。syscall 编号复用
asm-generic 表（`__aarch64__ || __riscv` 合流）。TLS LE 重定位已实现
（TPREL_LO12_I/S/HI20），`test/riscv64_e2e.sh` exit=42 验证通过。

### arm

**已实现**（2026-07-27 完成，ar mv7 → arm 标准化命名）。

使用 ARMv7 hard-float EABI（AAPCS），子架构配置支持 `-march`/`-mcpu`/`-mfpu`/`-mfloat-abi`。
明确区分整数寄存器参数（R0-R3）与 VFP 参数。8 字节参数按 AAPCS 对齐和结构体返回规则处理。
线程指针使用 `__kuser_set_tls` 内核辅助（0xffff0fe0），无需特权指令。

**运行时文件清单**：
| 文件 | 职责 |
|------|------|
| `crt1.S` | _start 入口：设置 tp、传 argc/argv/envp、调用 main、exit |
| `src/arch/arm/syscall.S` | EABI syscall gate（R7 放编号，参数 R0-R5） |
| `src/arch/arm/atomic.S` | C11 原子：1/2/4/8 字节（LDREX/STREX + DMB） |
| `src/arch/arm/setjmp.S` | setjmp/longjmp：保存 s0-s11/sp/ra + fs0-fs7 |
| `src/arch/arm/sigreturn.S` | rt_sigreturn restorer |
| `src/arch/arm/thread_clone.S` | clone 入口：子线程栈切换 + 调用 entry |
| `src/arch/arm/set_tls.S` | __kuser_set_tls 封装 |
| `src/arch/arm/tls.c` | TLS 初始化 variant I（GAP=0） |
| `src/arch/arm/aeabi.c` | AEABI 运行时辅助函数 |
| `src/arch/arm/aeabi_wrap.S` | AEABI 除法/移位 wrapper |

**验证**：`test/arm-bootstrap.sh` 6 测试全过（cross-gcc 编译链接）；qemu-arm 运行时 hello/atomic/setjmp/exit=42 验证通过。

## 5. 推荐移植顺序与验收

1. **x86_64 保持绿**：所有公共行为先通过现有 `make -C projects/meuos-libc check`。
2. ✅ **i386 收口**：跨函数 `va_list`、time64 类型与 syscall、x87 浮点完整、
   FPR 收口、socketcall 多路复用、qemu 端到端 gate 均已完成
   （`make check-i386-qemu` 全绿）。剩余 TLS/信号上下文
   （bare_tls 被 mcc i386 TLS 模型缺口阻塞，见 `projects/mcc/.todo/gd-tls.md`）。
3. ~~aarch64~~ ✅：crt1 + syscall gate + atomic + setjmp + sigreturn +
   thread_clone + set_tls + tls.c 全套到位，`Makefile ARCH=aarch64` 规则注册，
   `test/aarch64-bootstrap.sh` 提供跨编译自检 + 可选 qemu-aarch64-static
   运行时 gate（hello/atomic/phase2_counter=2000/bare_tls）。作为第一条
   64 位跨 ISA 完整链，riscv64/loongarch64 可参照其工作流。
4. ✅ **loongarch64**：crt1 + syscall gate + atomic + setjmp + sigreturn +
   thread_clone + tls.c 全套到位（移植自 riscv64），`Makefile ARCH=loongarch64`
   规则注册，`test/loongarch64-bootstrap.sh` 提供跨编译自检 + 可选
   qemu-loongarch64 运行时门禁。移植过程中发现了 riscv64 遗漏的 21 个 wrapper
   条件分支 bug（`__aarch64__` 路径未含 `__riscv`），已一并修复。代码经静态
   核对，真机门禁待交叉工具链/qemu 就绪。
5. ~~riscv64~~ ✅：crt1 + syscall gate + atomic + setjmp + sigreturn +
   thread_clone + tls.c 全套到位（移植自 aarch64），`Makefile ARCH=riscv64`
   规则注册，`test/riscv64-bootstrap.sh` 提供跨编译自检 + 可选 qemu-riscv64
   运行时门禁。代码经静态核对，真机门禁待交叉工具链/qemu 就绪。作为第三条
   64 位完整链，反向校验了公共层架构抽象（syscall.h 合流 asm-generic、setjmp.h
   增量分支）。
6. ✅ **arm**：libc runtime 完整（含 AEABI 兼容层），`make ARCH=arm` + qemu-arm 运行时验证通过（hello/atomic/setjmp/exit=42）。作为第一条 32 位 ARM 完整链，验证了 `__kuser_set_tls` 内核辅助路径、AEABI 兼容层和硬浮点 ABI。
7. **ppc64le/s390x**：仅在明确用户空间需求或有可用 QEMU/硬件门禁时排期。
8. **armel/mips\***：保持排除清单，不创建默认 target、不在核心库引入兼容性分支。

每个新 target 的最小验收必须同时包括：

- `make ARCH=<arch>` 产出 `crt1.o`、`libc-meuos.a`、`libatomic-meuos.a`；
- 无宿主 libc 的静态程序能运行 `write`、`malloc`、stdio、C11 atomic；
- TLS、`thrd_create/thrd_join`、futex、signal/setjmp 回归；
- `time_t` 宽度和 2038 边界（32 位目标）；
- 通过对应 QEMU VM 或真实硬件运行，而不只检查汇编文件能否生成；
- 目标源码中不出现 glibc 私有头、LLVM/GCC 代码或宿主 ABI 假设。
