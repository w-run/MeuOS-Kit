# meuos-libc - 目录结构与模块索引

> 本文档是 meuos-libc 的导航地图，供 AI agent 与人类阅读。
> 自举管线上下文见 `../../AGENTS.md` §2.1 与 `../../STATE.md`。
> 多架构状态、ABI 契约与移植顺序见 [`PORTING.md`](PORTING.md)。

## 1. 概述

`meuos-libc` 是直接面向 Linux 内核 ABI 的 MeuOS C 库：系统调用封装不经过
宿主 libc，直接 `syscall()` 或内联汇编。当前以 x86_64 为完整运行验证基线，i386 有最小 bootstrap；
**aarch64** 已完成 libc runtime（crt1 + syscall gate + atomic + setjmp + sigreturn +
thread_clone + set_tls + tls.c），`test/aarch64-bootstrap.sh` 提供跨编译自检 +
qemu-aarch64-static 运行时 gate（hello/atomic/phase2_counter=2000/bare_tls 端到端通过）。
**arm** 已完成 libc runtime（含 AEABI 兼容层），qemu-arm 运行时验证通过（hello/atomic/setjmp/exit=42）。
**riscv64、loongarch64** 的 libc runtime 已落地代码（crt1/syscall/atomic/setjmp/sigreturn/
thread_clone/tls.c 全部就绪），`make ARCH=riscv64`/`make ARCH=loongarch64` 已注册，
qemu 运行时门禁待交叉工具链就绪。目标是支撑 mcc 与 meow 的最小自举。

按 AGENTS.md §2.1，核心库只暴露标准符号；任何 GNU 扩展符号
（`error_at_line`、`obstack`、`argp` 等）全部放入独立 `meuos-libc-compat`
归档，应用显式 opt-in。

## 2. 目录树

```
meuos-libc/
├── Makefile                  # 顶层构建（libc-meuos.a / libatomic-meuos.a / crt1.o）
├── ARCHITECTURE.md           # 本文件（目录结构与模块索引）
├── PORTING.md                # 多架构状态、ABI 契约与移植说明
├── include/                  # 公共 libc 头文件
│   ├── stdio.h stdlib.h string.h stdint.h stdatomic.h threads.h ...
│   └── sys/                  #   系统头文件（syscall.h / stat.h / mman.h / ...）
├── crt/                      # 启动入口（每 arch 一个 crt1.S）
│   ├── x86_64/crt1.S
│   ├── i386/crt1.S
│   ├── aarch64/  (✅ ���完成：crt1.S / atomic.S / setjmp.S / sigreturn.S / thread_clone.S / set_tls.S / tls.c)
│   ├── riscv64/  (✅ 已完成：crt1.S / atomic.S / setjmp.S / sigreturn.S / thread_clone.S / tls.c)
│   ├── loongarch64/ (✅ 已完成：crt1.S / atomic.S / setjmp.S / sigreturn.S / thread_clone.S / tls.c)
│   └── arm/      (✅ 已完成：crt1.S / atomic.S / setjmp.S / set_tls.S / sigreturn.S / thread_clone.S / tls.c / aeabi.c / aeabi_wrap.S)
├── src/
│   ├── arch/<arch>/          # arch 专属运行时（atomic / setjmp / sigreturn / thread_clone / tls）
│   │   ├── x86_64/            #   完整实现
│   │   ├── i386/              #   最小 bootstrap（含 load_gs / soft_arith）
│   │   ├── aarch64/ (✅ 已完成)  riscv64/ loongarch64/  (✅ 已完成：runtime 全部就绪，见 .todo 验证项)
│   │   └── arm/     (✅ 已完成：含 AEABI 兼容层 + __kuser_set_tls，qemu 验证通过)
│   ├── internal/              # 内部头 + syscall gate
│   │   ├── syscall.h          #   syscall 编号翻译 + __syscall6 声明
│   │   └── arch/<arch>/syscall.S  # 每 arch 的 syscall 入口
│   ├── stdio/                 # 分拆的 FILE 流（file/char_io/block_io/position/fmt_in/fmt_out/...）
│   ├── stdlib/                # malloc / exit / env / convert / process / search / math
│   ├── string/                # memory / duplicate / error / token
│   ├── syscall/               # 每个 syscall 一个独立 .c（read/write/open/...，共 40+）
│   ├── thread/                # C11 线程（c11_threads/mutex/condvar/tss/call_once/state/pthread）
│   ├── signal/                # signal / sigaction / sigsetjmp
│   ├── ctype/                 # ctype.h
│   ├── errno/                 # errno.h
│   ├── dirent/                # opendir/readdir/...
│   └── compat/                # GNU 扩展兼容层（独立归档 libc-meuos-compat.a）
│       ├── Makefile           #   独立构建，产出 libc-meuos-compat.a
│       ├── include/           #   argp.h error.h obstack.h string2.h
│       └── src/               #   argp/asprintf/error/getline/malloc-hooks/obstack
└── test/                      # 回归测试（atomic/threads/TLS/stdio/process/...）
```

## 3. 模块职责

| 目录 | 职责 |
|------|------|
| `include/` | 公共标准头文件；`sys/` 下为系统头。核心库只暴露标准符号。 |
| `crt/<arch>/` | `_start` 入口：设置 TLS、传 argc/argv/envp、调用 main、exit。 |
| `src/arch/<arch>/` | arch 专属运行时：原子操作、setjmp/longjmp、sigreturn、线程 clone、TLS。 |
| `src/internal/` | 内部头（`syscall.h`）+ 每 arch 的 syscall gate（`arch/<arch>/syscall.S`）。 |
| `src/syscall/` | 每个 Linux syscall 一个 `.c`，直接调 `__syscall6`/`__syscall_number`。 |
| `src/stdio/` | 最小 FILE 流：`file.c`(fopen/fdopen/...)、`fmt_out.c`(vfprintf/snprintf 内核)、`fmt_in.c`(scanf)、`block_io`/`char_io`/`position`/`memory_stream`。 |
| `src/stdlib/` | first-fit malloc/calloc/realloc/free、exit、env、strtod/strtol、process(system)、search(bsearch/qsort)、math。 |
| `src/string/` | memcpy/memmove/memset/strlen/...、strdup、strerror、strtok。 |
| `src/thread/` | C11 threads(threads.h) + pthread 适配，基于 clone/futex。 |
| `src/signal/` | signal/sigaction/kill/sigprocmask/sigaltstack + SA_RESTORER。 |
| `src/ctype/` `src/errno/` `src/dirent/` | 对应标准头实现。 |
| `src/compat/` | 独立归档 `libc-meuos-compat.a`：argp/error/obstack/getline/asprintf/funopen 等 GNU 扩展。 |

## 4. 构建产物

| 产物 | 内容 |
|------|------|
| `libc-meuos.a` | 核心库：string/stdlib/stdio/syscall/thread/signal/ctype/errno/dirent + arch runtime + syscall gate |
| `libatomic-meuos.a` | 仅 `arch/<arch>/atomic.o`，供需要原子 runtime 的程序显式链接 |
| `libgcc-meuos.a` | libgcc ABI 软 helper 独立归档（`__divdi3/__ctzdi2/__floatsidf` 等 44 符号），gcc/clang 无厂商 libgcc 时链接用；不污染 libc 符号表 |
| `crt1.o` | 启动入口 |
| `libc-meuos-compat.a` | GNU 扩展兼容层（由 `src/compat/Makefile` 独立构建） |

## 5. 入口契约（entry contract）与分层

`crt1.S` 进入 `main()` 前调用 `__meuos_startup(argc, argv, envp)`（`src/startup.c`），
捕获进程镜像：`argv[0]` 与 auxv 基址。

| 符号 | 归属 | 说明 |
|------|------|------|
| `__progname` | core | BSD/glibc 惯用，进程名（argv[0]） |
| `getauxval` | core | 读取 auxv（`<sys/auxv.h>` + AT_* 常量） |
| `program_invocation_name` / `program_invocation_short_name` | **core（数据）+ compat（声明头）** | glibc 惯例启动信息；见下方偏离记录 |

### 记录：program_invocation_* 数据为何落 core（偏离 compat）

`program_invocation_*` 属 glibc 惯例符号，按分层铁律本应进 opt-in 的 compat
归档（只暴露标准符号于 core）。实际落地为 core 拥有数据、compat（
`program-invocation.h`）拥有公开声明面，是**工具链能力缺口**驱动的必要偏离，
`musl` 亦将此类 argv[0] 驱动的启动信息放 libc 核心。具体限制（均实证）：

1. **crt 无 `.init_array`**：6 架构 `crt1.S` 均无 `.init_array`/`__libc_start_main`
   wrapper，opt-in compat 归档没有启动钩子在运行时填值。
2. **core 对未定义 weak 符号的写被工具链打爆**：`extern char *x (weak); x=...`
   → 链接器生成「从地址 0 的 pcrel load」→ 运行 SIGSEGV（gdb 回溯 __meuos_startup+0x34）；
   工具链未把 unresolved weak 折叠成 0 常量引用。
3. **GAS `.set` 别名跨 archive 不可链接**：汇编 `.set a, __progname` 不产生消费者
   可解析的公开符号；GCC `__attribute__((alias))` 又要求同 TU 定义目标。

> 根治方向：给 crt 加 `.init_array`/`__libc_start_main` 支持，或修复上传符号折叠，
> 之后可评估将 `program_invocation_*` 数据移回 compat（登记于
> `.todo/meuos-libc/init-array-compat-data-hook.md`）。core **不**做 `__GLIBC__` 伪装。

## 6. 多 arch 构建

`make ARCH=<arch>` 切换目标：
- `x86_64`（默认）：宿主 `cc` 编译 C，宿主汇编器处理 `.S`；当前完整运行验证基线。
- `i386`：`$(MCC) --target=i386` 编译 C，宿主 `cc -m32` 汇编；当前只承诺整数 bootstrap。
- `aarch64`：libc runtime + qemu gate 端到端通过（**第一条 64 位跨 ISA 完整链**）；
- `arm`：libc runtime + qemu 运行时验证通过（hello/atomic/setjmp/exit=42）；`make ARCH=arm` 已注册。
- `loongarch64`/`riscv64`：runtime 代码已落地，待交叉工具链就绪验证。

`src/internal/syscall.h` 的 `__syscall_number()` 把 x86_64 syscall 号翻译为目标
arch 的原生编号；ABU 形状不同的 syscall（如 i386 的 mmap2/socketcall）用
专用 wrapper 而非静默翻译。

## 7. 待实现项

见 [`PORTING.md`](PORTING.md) 的架构状态、ABI 契约、time64 策略和验收清单；
各 `src/arch/<arch>/.todo` 记录具体 runtime 移植任务，根目录 `.todo/` 记录跨架构
功能（例如 32 位 time64、原生链接器）。
