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
- mt/ld 动态链接：`projects/meuos-toolchain/ARCHITECTURE.md:228-268` 已规划 -shared/-ld.so/dlopen 端到端
- libc 侧 dlfcn：`src/dlfcn.c` 实现 dlopen/dlsym/dlclose/dlerror，**但仅 x86_64**（Makefile 排除 i386/arm）

---

## ✅ 已闭环 P0

所有 8 项 P0 缺口已由本 worker（libc-features-p0）完成实现、验证、提交。

### ✅ 2.1 features.h 与特性测试宏
- 实现：`include/features.h`（musl 路线：`_GNU_SOURCE=1` 默认放开，薄壳 `__USE_GNU`/`__USE_MISC` 等）
- 测试：自动覆盖（check-integration mini-bc 通过 gcc + sysroot 隐式验证）
- 提交：主支（mcc-dev）早期已含

### ✅ 2.2 `__progname` / `program_invocation_name` / `program_invocation_short_name`
- 实现：`src/startup.c`（`__meuos_startup()` 从 argv[0] 写入）
- 声明：`src/compat/include/program-invocation.h`（compat 层）
- 验证：`test/auxv.c` 在 `make check` 中已验证 `__progname == argv[0]`

### ✅ 2.3 `getauxval`
- 实现：`src/startup.c` 实现 `getauxval(unsigned long)` + 内部 `__auxv_cache`（由 `__meuos_startup()` 缓存）
- 头文件：`include/sys/auxv.h` 声明 + AT_* 常量全集（AT_NULL ~ AT_EXECFN）
- 验证：`test/auxv.c` 验证 `getauxval(AT_PAGESZ)` 非零 / 4K 对齐

### ✅ 2.4 `__ctype_b_loc` / `__ctype_tolower_loc` / `__ctype_toupper_loc` + 平表
- 实现：`src/ctype/ctype.c` ——
  - 函数指针形式：`__ctype_b_loc()` / `__ctype_tolower_loc()` / `__ctype_toupper_loc()`（glibc 风格）
  - 平表形式：`__ctype_b` / `__ctype_tolower` / `__ctype_toupper`（musl 风格）
  - 两视图共享同一后备存储（懒初始化），保证字节级一致
- 声明：`include/ctype.h` 增加 `__ctype_b_loc` / `__ctype_tolower_loc` / `__ctype_toupper_loc` 声明 + 平表 extern
- 验证：`test/p0_ctype_tables.c` 在 `make check` 中全 PASS

### ✅ 2.5 crti.o / crtn.o + .init/.fini 三件套
- 实现：6 架构（x86_64/aarch64/arm/i386/riscv64/loongarch64）各 `crti.S`（prologue: push rbp; mov rbp,rsp）和 `crtn.S`（epilogue: pop rbp; ret）
- Makefile：增加 `$(BUILD)/crti.o`、`$(BUILD)/crtn.o` 目标 + install 装到 `/usr/lib/`
- 测试：`test/p0_crti.c` 用 `__attribute__((constructor))` / `__attribute__((destructor))` → 期望 trace "CMD"
- 验证：`check-integration` 用宿主 gcc + crti.o + crt1.o + crtn.o + libc-meuos.a + libgcc-meuos.a 链接 p0_crti.c → `CMD` PASS
- 提交：2e13fb44

### ✅ 2.6 `__libc_start_main` 形式
- 实现：6 架构 `crt1.S` 已全部改为 GNU signature `__libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)`
- 实现：`src/startup.c` 包含 `__libc_start_main()`（walk .preinit_array/.init_array/.fini_array, call main, exit）
- `_init` / `_fini` 为 weak 声明（兼容无 crti/crtn 场景）
- .init_array / .fini_array gate 已通过 `test/initarray.c` + `test/initarray_arr.S`
- 验证：`make check` 中的 initarray gate 全 PASS

### ✅ 2.7 libgcc-meuos.a 独立归档
- 实现：`src/libgcc_meuos/` 含 4 文件覆盖 30+ 符号：
  - `bit.c`：`__clzsi2/__clzdi2/__ctzsi2/__ctzdi2/__popcountsi2/__popcountdi2/__paritysi2/__paritydi2/__bswapsi2/__bswapdi2`
  - `conv.c`：`__floatsisf/__floatsidf/__floatunsisf/__floatunsidf/__floatdisf/__floatdidf/__floatundisf/__floatundidf/__fixsfsi/__fixdfsi/__fixsfdi/__fixdfdi/__fixunssfsi/__fixunsdfsi/__fixunssfdi/__fixunsdfdi/__extendsfdf2/__truncdfsf2`
  - `div.c`：`__udivdi3/__divdi3/__umoddi3/__moddi3/__udivmoddi4/__divmoddi4/__udivti3/__divti3/__umodti3/__modti3/__udivmodti4/__divmodti4`
  - `mul.c`：`__muldi3/__multi3`
- Makefile：已纳入 `all` 目标和 `install`；`check` 中 nm 验证 15+ 关键符号存在
- 验证：`test/libgcc.c` 在 `make check` 全 PASS；`check-integration` 中 gcc 编译 mini-bc 隐式验证

### ✅ 2.8 ISO C23 `<stdbit.h>` + `<stdckdint.h>` + `<stdcountof.h>` + `timespec_getres`
- `<stdbit.h>`：C23 7.18 bit 操作宏族 `stdc_leading_zeros` / `stdc_trailing_zeros` / `stdc_count_ones` / `stdc_has_single_bit` / `stdc_bit_width` / `stdc_bit_floor` / `stdc_bit_ceil` —— mcc 兼容（不使用 `__builtin_*`）
- `<stdckdint.h>`：C23 7.21 `ckd_add` / `ckd_sub` / `ckd_mul` —— mcc 兼容（`({})` + `_Generic`，`__typeof__(&(result))` 在括号外）
- `<stdcountof.h>`：C23 7.6.3 `countof` —— `sizeof(arr)/sizeof(arr[0])` + `_Generic` 指针检测防护
- `timespec_getres`：`src/time/time.c` 已有实现 + `include/time.h` 声明 + `src/syscall/clock_getres.c`（syscall 229）
- 验证：`test/p0_c23.c` 通过（待加入 make check）

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

1. **`include/features.h` + 特性测试宏体系**（半天，含 _GNU_SOURCE 默认开、所有现有头按条件 #ifdef `__USE_GNU` 暴露 GNU 符号） — 这一项解锁 autotools/cmake/meson 全部探测合约。
2. **`__progname`/`__progname_full`/`program_invocation_name` + `getauxval` + `__ctype_b` 系列平表**（一天） — 解锁 syslog/error/perl/python 探测。
3. **crti.o / crtn.o + __libc_csu_init + __libc_start_main 三件套**（与 mt/ld 端到端挂上，整 2 天） — 解锁 gcc/clang 默认构造函数/析构函数处理 + `__libc_start_main` 链。
4. **libgcc-meuos.a 独立归档**（3-4 天，含 x86_64/aarch64/riscv64/loongarch64 + 32 位目标的通用 .c 软算术与位操作实现） — 解锁 gcc 编译 64 位除 / `__builtin_ctz` / 软浮点。
5. **ISO C23 `<stdbit.h>` + `<stdckdint.h>` + `timespec_getres`**（半天） — 解锁 gcc -std=c2x 默认头查找。