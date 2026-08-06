# meuos-libc 升级为「系统 libc」差距清单

> 调研时间：2026-08-05
> 视角：从「mcc/meow 自举最小 libc」升级为「取代 glibc/musl 的 MeuOS 系统 libc」，且支持 gcc/clang 用其作为 sysroot libc 编译目标。
> 约束（AGENTS.md §4）：零 GNU/glibc 依赖、核心库只暴露标准符号（GNU 扩展走 meuos-libc-compat 独立归档）、直接 syscall 不走宿主 libc、可重现构建。

---

## 1. 已就位（基础完成）

- ISO C99/C11 头 50+：`include/` 覆盖 stdio/stdlib/string/... 全家族
- POSIX 头全部存在：pthread（含屏障/自旋/读写锁）、sched、spawn、glob、regex、sys/mman、sys/stat、sys/select、sys/socket、sys/wait、termios、netdb、dirent
- 6 架构 ABI/crt1/runtime 完整：x86_64/i386/aarch64/arm/riscv64/loongarch64
- TLS runtime：x86_64 fs、i386 gs、aarch64 TPIDR_EL0、arm __kuser_set_tls、riscv64/loongarch64 直接 mv tp
- errno 用 `_Thread_local`（src/errno/errno.c:5 已是 _Thread_local）
- syscall gate：`src/internal/syscall.h` 维持 x86_64 编号为内部稳定号，按 arch 翻译
- strcasecmp/strncasecmp 已在 `include/string.h:33-34` 进核心
- argp/error/obstack/asprintf/getline/funopen 在 `src/compat/` 独立归档
- mt/ld 动态链接：`projects/meuos-toolchain/ARCHITECTURE.md:228-268` 已规划 -shared/-pie/ld.so/dlopen 端到端
- libc 侧 dlfcn：`src/dlfcn.c` 实现 dlopen/dlsym/dlclose/dlerror，**但仅 x86_64**（Makefile 排除 i386/arm）

---

## 2. P0 缺口（缺了 gcc/clang 编译合约立刻挂）

### 2.1 features.h 与特性测试宏（**零**，grep 完全无踪迹）
- 缺：`<features.h>`、`__GLIBC__`/`__GLIBC_MINOR__`、`_GNU_SOURCE`/`_DEFAULT_SOURCE`/`_POSIX_C_SOURCE` 任何形式的开/关机制。
- 后果：autotools 探测 `#ifdef _GNU_SOURCE` 后乱配；musl 已展示「显式 _GNU_SOURCE=1 默认放开」是更稳的方向。sysroot 装 libc 后没 features.h，应用编译直接 #error。
- 位置：**进核心**（`include/features.h`，最小化：识别 `_GNU_SOURCE` 和 `_POSIX_C_SOURCE`，按值开/关 `__USE_GNU`/`__USE_MISC` 等薄壳宏）。

### 2.2 `__progname` / `__progname_full` / `program_invocation_name`
- 缺：errno/stdio 已有 `environ`，但启动信息变量完全缺。syslog / error / bash 都从 argv[0] 取，gcc 编译 glibc 兼容的应用需要。
- 位置：crt1 里取 argv[0] 写入全局 → **进核心**。

### 2.3 `getauxval`
- 缺：`<unistd.h>` 里完全没有。dtv base、AT_RANDOM（canary）、AT_PLATFORM、AT_EXECFN、AT_SYSINFO_EHDR 都无入口。
- 后果：动态加载、`__stack_chk_guard`、bpf/libcap 都读 auxv；没有 getauxval 就得在 glibc-compat 应用里 stub。
- 位置：crt1 把 auxv 指针存到 TCB，`__getauxval(unsigned long)` 在 glibc 兼容里返回 → **进核心**。

### 2.4 `__ctype_b_loc` / `__ctype_tolower_loc` / `__ctype_toupper_loc`
- 缺：musl 用平表 `__ctype_b` 直接导出。glibc 用函数指针（线程安全）。既无函数指针形式也无平表，应用 dlsym 都拿不到。
- 后果：部分 perl/python/qemu 直接 `__ctype_b_loc()` 探测。
- 位置：选 musl 路线，平表 `__ctype_b`/`__ctype_tolower`/`__ctype_toupper` → **进核心**。

### 2.5 crti.o / crtn.o + .init/.fini + `__libc_csu_init`
- 缺：Makefile 只产 `crt1.o`。**crti.o/crtn.o 与 crt1.o 三件套**才符合 gcc/clang 默认 `-init`/`-fini` 处理。
- 后果：`__attribute__((constructor))` / `__attribute__((destructor))` 触发 .init_array/.fini_array 处理，没有 crti/crtn 就被链接器 ld 抱怨 undefined reference。
- 位置：**进核心**（Makefile 增加产物，crt/*.S 增加 crti/crtn）。

### 2.6 `__libc_start_main` 形式 / `__libc_csu_init` / `__libc_csu_fini`
- 缺：当前 `_start` 直进 main，无 libc 启动 wrapper。gcc 默认链接 `__libc_start_main`（即 GNU libc ABI），也没有 `__libc_csu_init`。
- 后果：gcc/clang 编译任何用 `main()` 的程序，链接时找不到 `__libc_start_main`。
- 位置：**进核心**（crt1.S 改成 `__libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)` 形式；静态链接路径保留 `_start` 直接入口）。

### 2.7 libgcc / compiler-rt 兜底符号
- arm AEABI 已有（`src/arch/arm/aeabi.c`+`aeabi_wrap.S`）。i386 Kl 软算术已有（`src/arch/i386/soft_arith.c`）。
- 但：**x86_64/aarch64/riscv64/loongarch64 缺通用 `__divdi3`/`__udivdi3`/`__moddi3`/`__umoddi3`/`__muldi3`/`__ctzdi2`/`__clzdi2`/`__popcountdi2`/`__bswapdi2`/`__floatsidf`/`__floatunsidf`/`__floatdisf`/`__fixsfdi`/`__udivti3`/`__multi3`** 等 30+ 符号。
- 后果：gcc 编译 c 代码做 64 位除 / `__builtin_ctz` / 软浮点时不带 libgcc 即报 `undefined reference to __divdi3`。
- 位置：**独立 `libgcc-meuos.a`**（不污染 libc），与现有 `libatomic-meuos.a` 同位。可选方式：`make libgcc` 子目标编译 `src/libgcc_meuos/` 通用 C 实现，约 30 文件覆盖所有有 64 位除/位操作/软浮点符号。

### 2.8 ISO C23 `<stdbit.h>` + `<stdckdint.h>` + `<stdcountof.h>` + `timespec_getres`
- 缺：三个头完全缺失；`timespec_getres` 在 `<time.h>` 也没声明。
- 后果：gcc -std=c2x（默认 gcc 14+）编译器内键 `__builtin_stdc_*` 落不到 libc 头里。
- 位置：**进核心**（三头 + `timespec_getres` 简单实现）。`ckd_add/sub/mul` 是宏，不需 .c。

---

## 3. P1 缺口（典型应用可见）

### 3.1 POSIX 面
- `<sys/uio.h>` `readv/writev/preadv/pwritev/preadv2/pwritev2`：完全缺。
- `*at` 全家：`openat/faccessat/fchmodat/fchownat/fstatat/mkdirat/readlinkat/symlinkat/unlinkat/linkat/renameat/mknodat`、`<sys/stat.h>` 缺。
- `pipe2/dup3`：`<unistd.h>` 缺。
- `<spawn.h>` 头在，**实现状态没在 src/syscall 看**（Makefile 没列 spawn.c），基本是占位头，需补 posix_spawn + 完整 file_actions_attr API。
- termios 全操作：`cfmakeraw/cfsetspeed/tcsendbreak/tcflush/tcflow/tcgetsid` 等需核 `src/termios/termios.c`（应为部分实现）。
- `<time.h>` 缺：`clock_getres/clock_settime/clock_nanosleep/timer_create/timer_settime/timer_delete/mq_notify`。
- POSIX errno 全集：当前 `include/errno.h` ~30 个，远不足 POSIX 130+（缺 ECANCELED-class ~30；已部分有 ECANCELED/ETIMEDOUT/EALREADY 但缺 EOWNERDEAD/ENOTRECOVERABLE/EOVERFLOW/EWOULDBLOCK/EHOSTDOWN 等）。
- `<wordexp.h>` wordexp/free：`src/stdlib/` 没，看缺（bash 关键）。
- `<wctype.h>` / wide stdio `swprintf/vfwprintf/fwprintf/...`：`<wchar.h>` 有 iswalnum 但缺宽 stdio 全部。
- pthread_attr_setstack / pthread_attr_getstack / pthread_attr_setguardsize：`src/thread/pthread.c` 还没实现。
- `getopt_long` / `getopt_long_only`：compat 头没列，应用探测直接挂。
- `strsignal` / `strcasestr` / `psignal`：缺。
- `strncasecmp` 在核心 string.h，无需补。

### 3.2 glibc 公共变量/符号
- `optarg/optind/opterr/opopt`：compat 没列，`<getopt.h>` 都没头。
- `__environ` 别名：environ 已在，__environ 别名缺失。
- `h_errno`/`h_errlist`（netdb）：`src/netdb/h_errno.c` 已构建，但 `h_errno` 是否定义为宏。

### 3.3 32 位 libc 收口
- i386 `dlfcn.o` 在 Makefile 排除，i386/arm 32 位无 dlfcn。考虑 i386/aarch32 用 reduced stub。
- i386 TLS 端到端测试被 `mcc/.todo/gd-tls.md` 阻塞，独立缺陷。

### 3.4 widl stdio 实现
- `<stdio.h>` 没 swprintf/vfwprintf/fwprintf/fwide/fputwc 已 export 但 src/wchar/wchar.c:165 仅 thin wrapper，需把 fwide/swprintf 实装进 wchar.c。

---

## 4. P2 锦上添花

### 4.1 libc 动态库形态（`.so`）
- 现状：所有 .c 都是非 PIC 编译产物成静态 `libc-meuos.a`。mt/ld 已有 `-shared` 支持，但 libc 自身还没 PIC。
- 路径：写 `make -C projects/meuos-libc shared` 子目标，把所有 C 文件 `-fPIC` 重编译出 `libc-meuos.so` + 版本符号 SONAME。
- 不必 P0，因为 GCC sysroot 也可以完全静态链接使用 libc-meuos.a（仅有 PIC 不是硬合约）。

### 4.2 `<tgmath.h>` / `<uchar.h>` / `<bits/*>` / `<stdcountof.h>` 内置
- 当前都没，但 gcc 默认不强制要求，autotools 偶有探测。

### 4.3 `dl_iterate_phdr`
- 动态加载器反馈，初级 sysroot 用不上。

### 4.4 BSD `<strings.h>` `bcopy/bzero/bcmp/ffs/index/rindex`
- 历史代码才用，新 GNU 应用基本不用。

### 4.5 `mt/locale` C locale 完整数据
- 当前 `<locale.h>` 估计仅占位，C locale 数据太细可后续。

### 4.6 `mqueue` / `aio` / REALTIME thread 时钟
- 实时 POSIX，user 空间常见度低。

---

## 5. compat 层现状清单

`src/compat/` 现有：argp / error_at_line / obstack / asprintf / getline / fopencookie / funopen / malloc-hooks / strdupa / strndupa。Makefile 自动归档 `libc-meuos-compat.a`。

需补（按 P0 → P2）：
- **P0**：`getopt_long/getopt_long_only`（compat；因为 GNU 扩展）
- **P1**：`strsignal/psignal`（compat；bsd/gnu 公共）
- **P1**：`posix_spawn` 全套如果不上核心，则放 compat 错误，**实际应上核心**（P0 +1）
- **P2**：`wordexp`（compat 慎选，bash 用）
- **P2**：`crypt/setkey`（compat；shadow 体系不补 nss）

---

## 6. 首批落地建议（3-5 项）

按「性价比高 + 立刻解锁 gcc/clang sysroot」排序：

1. **`include/features.h` + 特性测试宏体系**（半天，含 _GNU_SOURCE 默认开、所有现有头按宏条件 #ifdef `__USE_GNU` 暴露 GNU 符号） — 这一项解锁 autotools/cmake/meson 全部探测合约。
2. **`__progname`/`__progname_full`/`program_invocation_name` + `getauxval` + `__ctype_b` 系列平表**（一天） — 解锁 syslog/error/perl/python 探测。
3. **crti.o / crtn.o + __libc_csu_init + __libc_start_main 三件套**（与 mt/ld 端到端挂上，整 2 天） — 解锁 gcc/clang 默认构造函数/析构函数处理 + `__libc_start_main` 链。
4. **libgcc-meuos.a 独立归档**（3-4 天，含 x86_64/aarch64/riscv64/loongarch64 + 32 位目标的通用 .c 软算术与位操作实现） — 解锁 gcc 编译 64 位除 / `__builtin_ctz` / 软浮点。
5. **ISO C23 `<stdbit.h>` + `<stdckdint.h>` + `timespec_getres`**（半天） — 解锁 gcc -std=c2x 默认头查找。
