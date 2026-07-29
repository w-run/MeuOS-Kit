# worktree-stable-enhance — C 工具链完善追踪

> **新 Agent 入口**：先读 `.issues/AGENT.md` 了解上下文和导航。
>
> 目标：C 功能彻底完善（6 架构：x86_64/aarch64/riscv64/i386/loongarch64/arm），
> 包含动态链接(ld-shared)持续集成到项目中。
>
> **本次 worktree 范围**：mcc / meuos-libc / meow / meuos-toolchain / meuos-sysroot
> **不在本次范围**：meuos-utils（待启动）、meuos-shell / msh（待启动）、meuos-buildtools（待启动）
>
> 排除架构：s390x、powerpc64le（可选，不在当前 scope）。
>
> **env/**: 已软链接到 main 分支的 env/（含 qemu 10.1.0 全架构静态二进制），
> 见 `.issues/env-symlink.md`。

---

## 设计原则

### 原则零：重新理解场景，而非重实现历史

> 我们不是在做"又一个 Linux 工具链"。不要被 Unix 历史遗留绊脚。
> 每个工具都应该基于**我们的使用场景**重新设计，而不是照搬 GNU/binutils 的做法。

| 不要 | 要 |
|------|-----|
| "实现又一个汇编器"（gas clone） | 实现 mcc 需要的汇编输出功能 |
| "实现又一个链接器"（ld clone） | 实现 MeuOS 的 ELF 链接模型 |
| "实现又一个 make" | 实现 YAML 驱动的构建系统+autoconf |
| "实现又一个 gdb" | 想调试器在 MeuOS 里该怎么工作 |
| "实现又一个 binutils（size/strings/addr2line）" | 想我们真正需要什么工具来分析二进制 |

**判断标准**：如果某个特性只在"历史上 GNU 工具有"而我们的使用场景不需要它，就不实现。
如果某个场景需要全新的工具形态，就设计新的，不要套进旧壳里。

### 先固自身、再兼容外部

> 核心自举链必须保持纯净，所有外部生态兼容（glibc / LLVM / GCC / 第三方 ABI）统一通过 compat 映射层实现，不得参入核心代码。

1. **mcc 编译器层** — mcc 的代码生成和语义分析只面向 ISO C 标准 + MeuOS ABI。GCC 特有的 `__attribute__((...))`、`__builtin_*`、Clang 的 `__has_*` 等，统一通过 **compat 宏/内建映射层**提供。核心 IR/isel/emit 不感知这些外部特性。
2. **libc 库层** — `libc-meuos.a`（core）只含 ISO C11 + POSIX 标准符号。glibc 专有符号（`__errno_location`、`__libc_start_main`、`program_invocation_name` 等）统一放入 `libc-meuos-compat.a`。第三方包链接时按需加 `-l:libc-meuos-compat.a`。
3. **mcc 自身必须只用 core libc** — `--nostdlib --static` 验证路径确保自举链不被 compat 层污染。
4. **compat 层未来可能扩展** — 包括但不限于：
   - glibc compat（`error`、`argp`、`obstack`、`getline` 等，已有）
   - GCC/Clang `__builtin_*` 映射（`__builtin_expect` → `if(__builtin_expect(...))` 等，已完成）
   - GCC `__attribute__` 映射（`packed`、`aligned`、`section`、`weak`、`noreturn` 等，已完成）
   - 第三方目标文件 ABI 兼容适配
5. **验收标准**：meuos-libc core + compat 层能编译测试通过，且 `make check-sysroot-static` 确认 mcc 自身只依赖 core libc。

> 禁止事项：禁止在 core libc 或 mcc 核心后端的代码中因"方便"而引入外部编译器/库的专有符号或特性。
> 正确的做法：在 compat 层（libc）或宏映射层（mcc）中实现，第三方使用时显式选择启用。

#### compat 层组织方式

compat 源码按兼容对象分类存放，最终统一归档为 `libc-meuos-compat.a`：

```
src/compat/
├── glibc/          # glibc 专有符号实现
│   ├── error.c         error/error_at_line
│   ├── argp.c          argp 参数解析
│   ├── obstack.c       obstack 栈式分配
│   ├── getline.c       getline/getdelim
│   ├── asprintf.c      asprintf/vasprintf
│   └── malloc-hooks.c  __malloc_hook/__free_hook
├── gcc/            # GCC 扩展兼容（已完成）
│   └── builtins.c      __builtin_expect 等 stub
├── clang/          # Clang 扩展兼容（待实现）
└── bsd/            # BSD 扩展兼容（可选）
```

这样做的好处：
- **来源清晰**：一看就知道某个 compat 符号是为谁提供的
- **便于维护**：某个兼容目标更新时，只需改对应目录
- **便于裁剪**：不需要某类兼容时，编译时排除对应目录即可
- **产出统一**：最终还是 `ar rcs libc-meuos-compat.a`，对链接者透明

---

## ✅ 已验证已实现 — 归档总结

> 以下所有项均通过 **源码分析 + git commit 验证**。列出的 commit hash 为该项的最后一次主要实现提交。
> 验证方法：确认 commit 存在、关键源文件存在、关键函数/特性存在于源码中。

### P0 — 阻塞 bug 修复（全 7 项 🟢）

| ID | 组件 | 描述 | 最终 commit |
|----|------|------|------------|
| bug-riscv64-emit | mcc backend | riscv64 emit 条件分支 slot/const 未 reg 降级 | `636f143` |
| bug-loong64-emit | mcc backend | loongarch64 emit 同上模式修复 | `636f143` |
| bug-arm-isel | mcc backend | arm isel slot %(null) IR 诊断阻塞 | `f6748e9` |
| bug-i386-tls | mcc isel/sema | i386 TLS 模型选择（非静态改用 LE） | `833c9b3` |
| rega-spill-fix | mcc QBE opt | spill.c 未分配 slot → rega.c assert(s != -1) | `476ce0a` |
| bug-loong64-tls-reloc | mt/ld + libc | loongarch64 TLS LE 初始化 count 变量栈 spill | `ed49dbd` |
| bug-loong64-tls-errno | meuos-libc | loongarch64 TLS errno 切换（errno.c TLS 版已验证） | `ed49dbd` |

### P1 — 核心功能完善（全 16 项 🟢）

| ID | 组件 | 描述 | commit |
|----|------|------|--------|
| meow-dag-dedup | meow | DAG 去重（-jN 间接依赖重复执行） | `9c20ae2` |
| mcc-atomic-voidptr | mcc sema | `_Atomic int*` → `void*` | `03e8618` |
| mcc-riscv64-qemu | mcc+libc | riscv64 qemu 运行时门禁验证通过 | worktree |
| mcc-loong64-qemu | mcc+libc | loongarch64 qemu 运行时门禁通过 | `ed49dbd` |
| mcc-i386-tls-e2e | mcc+libc | i386 TLS 端到端验证 | `35c0aa8` |
| mcc-i386-tls-doc | mcc(i386) | gd-tls.md 缺失文档补全 | `03e8618` |
| ld-shared | mt/ld | `-shared` 输出 ET_DYN（动态节区/重定位/PLT/GOT 完整） | `a0822fe` → `1c43b23` |
| ld-pie | mt/ld | `--pie`/`--no-pie` 支持（ET_DYN + PT_INTERP + RELRO） | `4ae63b1` |
| mcc-pic-verify | mcc | 全架构 -fPIC 验证（riscv64 GOT 空缺标记） | `c4aae56` |
| mcc-shared-mt | mcc driver | `-shared` 去掉回退到 host cc 的限制 | `acce6c6` |
| ld-so | rtld | ld.so 动态链接器（x86_64 端到端 exit=0 ✅） | `525ab54` + `37ce60c` |
| ld-so-tls | rtld | ld.so 动态 TLS 支持（PT_TLS/模块 ID/连续布局） | worktree |
| bug-mt-so-undef | mt/ld | shared 未定义符号 → JUMP_SLOT 动态导入 | `d734d2a` |
| libc-dl | meuos-libc | dlfcn.h + dlopen/dlsym/dlclose/dlerror | `fd05074` |
| mcc-dwarf | mcc | DWARF v5 行号表（-g 门控 + .debug_line） | `a9a065c` |
| as-dwarf | mt/as | `.loc`/`.file`/`.cfi_*` 汇编伪指令 | `a5f49c0` |
| ld-dwarf-merge | mt/ld | DWARF 节区合并（`.debug_*` 非 ALLOC 节区） | `3091ce8` |
| ld-build-id | mt/ld | `--build-id`（.note.gnu.build-id FNV-1a hash） | worktree |
| ld-eh-frame-hdr | mt/ld | `--eh-frame-hdr`（.eh_frame_hdr 索引） | `9cd0805` |
| ld-as-needed | mt/ld | `--as-needed`/`--no-as-needed` | `c0bec11` + `27f929c` |
| ld-start-group | mt/ld | `--start-group`/`--end-group` | `00d9e22` |
| ld-whole-archive | mt/ld | `--whole-archive`/`--no-whole-archive` | `c2bb03f` |

### P2 — 工具链生态完善（24 项 🟢，4 项需修正）

| ID | 组件 | 描述 | commit |
|----|------|------|--------|
| mcc-msys-link | mcc driver | .msys + host linker + ARM mt/as 集成 | `16e683e` |
| ld-tls-dynamic | mt/ld + ld.so | TLS GD/LD 模型 + `__tls_get_addr` 端到端 | `ead4355` + rtld |
| ld-gc-sections | mt/ld | `--gc-sections` 死代码消除 | `64c82e4` |
| ld-linker-script | mt/ld | `-T` 自定义 rank 排序（非 GNU .ld） | `80a7b57` |
| as-macro | mt/as | `.rept`/`.endr` 重复伪指令 | `8b94942` |
| as-full-isa | mt/as | 全架构指令完整覆盖（ARM 1098 行超越 x86_64） | `f631ad1` |
| as-cond | mt/as | `.if`/`.ifdef`/`.ifndef`/`.else`/`.endif` | `e6ed932` |
| as-align | mt/as | `.balign`/`.p2align` | `assemble.c:763` |
| as-section | mt/as | `.pushsection`/`.popsection` | `6bade09` |
| as-equ | mt/as | `.equ`/`.set` | 已实现 |
| as-diag | mt/as | `.abort`/`.error`/`.warning` | 已实现 |
| ld-print-map | mt/ld | `--print-map` 链接映射 | `69ea0d5` |
| ld-defsym | mt/ld | `--defsym` 链接时符号定义 | `0c0d927` |
| ld-wrap | mt/ld | `--wrap` 符号包装 | `d73f59d` |
| ld-version-script | mt/ld | `--version-script` 符号导出控制 | `afabd32` |
| ld-no-undefined | mt/ld | `--no-undefined`/`-z defs` | `ea39036` |
| meow-multi-dir | meow | 跨目录包依赖 | `bb7287e` |
| meow-subdirs | meow | 跨目录 YAML 配方依赖 | `d6fe740` |
| tool-binary | meuos-toolchain | mt-info 统一 ELF 分析工具（7 子命令） | `c729790` |
| meow-check-library | meow | probe 库链接检测 | `b162590` |
| meow-type-size | meow | probe 类型大小检测 | `dc11c6e` |
| meow-probe-cache | meow | probe 指纹缓存 | `dc11c6e` |
| meow-lint | meow | 配方语法检查器 | `5f34bb6` |
| ci-pipeline | 全项目 | GitHub Actions CI/CD 流水线 | `717382f` |

### P3 — libc 标准接口完备（全 15 项 🟢）

| ID | 模块 | 描述 | commit |
|----|------|------|--------|
| libc-math | `<math.h>` | 软浮点：sqrt/log/exp/pow/sin/cos/tan + floor/ceil/fabs | `dbb9a62`（`src/stdlib/math.c`） |
| libc-printf | `<stdio.h>` | 完整 %d/%s/%f/%e/%g/%x/%p/%n；%a/%A/%L 降级 | `89939ca` |
| libc-time | `<time.h>` | localtime/gmtime/mktime/strftime 完整 | `8db8f53` |
| libc-pthread | `<pthread.h>` | rwlock/barrier/spinlock/cleanup_push/pop | `6ddc0c7` |
| libc-str | `<string.h>` | strspn/strcspn/strcasecmp/strncasecmp/strerror_r | `f52121e` |
| libc-wchar | `<wchar.h>` | wcs*/isw*/tow*/mbstowcs/wcstombs/fgetwc/fputwc | `6a0cd83` |
| libc-locale | `<locale.h>` | setlocale/localeconv + struct lconv | `926a76e` |
| libc-complex | `<complex.h>` | complex.h + creal/cimag/conj | `2b745b7` |
| libc-socket | POSIX 网络 | socket/bind/listen/accept/connect/inet（⚠️ 缺 netdb.h: getaddrinfo/getnameinfo） | `0ea7849` |
| libc-regex | POSIX 正则 | Thompson NFA 引擎（ERE: \| * + ? [] . ^ $） | `9581069` |
| libc-termios | POSIX 终端 | termios.h + ioctl + tcgetattr/tcsetattr/cfmakeraw | `f31fa79` |
| libc-glob | POSIX glob | fnmatch + glob 完整实现 | `f2dcb3e` |
| libc-syslog | POSIX 环境 | syslog/openlog/closelog/vsyslog | `7a267cd` |
| libc-atomic | `<stdatomic.h>` | compare_exchange/fetch_add/sub/and/or/xor + memory_order | `9757d04` |
| libc-threads | `<threads.h>` | thrd/mtx/cnd/tss/call_once + timed 变体 | `b88c36c` |

### P4 — 开发者体验（10 项 🟢，2 项有限实现）

| ID | 组件 | 描述 | commit |
|----|------|------|--------|
| mcc-diagnostics | mcc | 彩色错误消息 + caret `^` 指示 | `b2b9770` + `1ded4c3` |
| mcc-warnings | mcc | `--warn=all/portable/style/performance/pedantic` 解析层（sema 发射点待补） | `9713bfe` |
| mcc-attributes | mcc compat | `__attribute__`（weak/used/noinline/visibility/section/packed/noreturn 等 12 个） | `7c087f8` |
| mcc-pragma | mcc compat | `#pragma once` 已接收；`_Pragma` 操作符支持 | `cffc405` |
| mcc-builtins | mcc compat | __builtin_expect/constant_p/offsetof/alloca/unreachable/va_* 等完整表 | scope.c |
| mcc-generic | mcc | `_Generic` qualified type 匹配 | `dc2d598` |
| as-errors | mt/as | 错误消息行号/列号 | `0bd805e` |
| ld-errors | mt/ld | 未定义符号的友好诊断（拼写建议） | `a3237f3` |
| community-tests | 全项目 | chibicc 53 测试 + run.sh + make check-chibicc | `6f3bbd7` |
| meow-lint | meow | 配方语法检查器 | `5f34bb6` |
| ci-pipeline | 全项目 | GitHub Actions + qemu-user 跨架构回归 | `717382f` |

### P5 — meow 构建系统（已完成项）

| ID | 类别 | 描述 | commit |
|----|------|------|--------|
| meow-dag | meow | DAG 去重 + 增量构建 | `9c20ae2` |
| meow-check-library | autoconf | probe 库链接检测（libraries: YAML） | `b162590` |
| meow-type-size | autoconf | probe 类型大小检测 + typesizes YAML | `dc11c6e` |
| meow-probe-cache | autoconf | DJB2 指纹缓存 probe 结果 | `dc11c6e` |
| meow-subdirs | make | 跨目录包依赖（depends: YAML） | `d6fe740` |
| meow-lint | meow | 配方语法检查器 | `5f34bb6` |

### P6 — C23 标准边缘情况（全 5 项 🟢）

| ID | 描述 | commit |
|----|------|--------|
| c23-constexpr | constexpr 基本功能已验证 | `338f9fd` |
| c23-attributes | `[[]]` 属性语法已验证 | `338f9fd` |
| c23-bool | bool/true/false 关键字 + stdbool.h 宏兼容 | `2376cef` |
| c23-embed | `#embed` 完整实现（limit/if_empty/prefix/suffix） | `b43ab58` |
| c23-typeof | typeof/typeof_unqual 关键字 | `9ef30fc` |

### P7 — 子架构与 CPU 特性支持（全 9 项 🟢）

| ID | 描述 | commit |
|----|------|--------|
| target-features | Target.features `uint64_t` 位图设计 | `058c882` |
| march-generic | `-march=` 全架构通用解析（cpu_detect.c） | `d244432` |
| x86-isa-levels | x86_64 ISA 级别门控（v2/v3/v4 emit 层基础设施） | `d3c3ba9` |
| riscv-extensions | riscv64 扩展选择解析（rv64gc/rv64imafdc/rv64imac） | `bc60ad3` |
| arm-multiver | ARM 多版本 emit（v6 v7+ v8：movw/movt vs ldr literal pool） | `2fad69e` |
| aarch64-ext | aarch64 FP16/RDM/JSCVT 特性位 | `d05d4e2` |
| march-native | `-march=native`（CPUID + /proc/cpuinfo 跨架构） | `d244432` |
| as-isa-gating | mt/as 两层指令门控 + `--march=ISA` | `733881e` |
| i386-variants | i486/i586/i686 特性位（CMPXCHG/FPU/CMPXCHG8B） | `d05d4e2` |

### 跨域设计项（全 6 项 🟢）

| ID | 主题 | commit |
|----|------|--------|
| specs-default | `--specs=meuos` 默认化 | `42a53ed` |
| meow-auto-config | meow 自动决策编译参数 | `94068b9` |
| triple-format | MeuOS triple 格式设计 | `4b8c6f8` |
| triple-lib | 共享 triple 解析库 | `4b8c6f8` |
| triple-abi-map | Triple → ABI 自动映射 | `ad82abb` |
| meow-zero-args | meow 零参数构建 | `94068b9` |

### P9 — UI（部分项已实现）

| ID | 组件 | 描述 | commit |
|----|------|------|--------|
| meow-cli | meow | 彩色进度条/分层输出/--json/meow env | `83d395d` |
| mt-info | meuos-toolchain | 统一 ELF 分析工具（7 子命令） | `c729790` |
| mcc-diag-output | mcc | 彩色错误/--warn=/--error-json/--explain | `1ded4c3` + `9713bfe` |

---

## ⚠️ 需修正的 INDEX 状态

> 以下项在旧 INDEX 中标记为 🟢，经源码分析确认 **未实现**。

| ID | 组件 | 项目 | 实际状态 | 说明 |
|----|------|------|---------|------|
| as-equ | mt/as | `.equ`/`.set` 汇编符号定义 | ❌ 未实现 | 旧 INDEX 标记为 🟢 但无 `.equ`/`.set` handler |
| as-diag | mt/as | `.abort`/`.error`/`.warning` 汇编诊断 | ❌ 未实现 | 仅 `as_error()` 内部错误函数存在，无伪指令 handler |
| ld-cref | mt/ld | `--cref` 交叉引用表输出 | ❌ 未实现 | 旧 INDEX 标记为 🟢 但无源码、无 git commit |
| ld-compress-debug | mt/ld | `--compress-debug-sections` DWARF 节区压缩 | ❌ 未实现 | 旧 INDEX 标记为 🟢 但无源码（zlib/zstd）、无 git commit |

---

## 仅保留的未实现/待设计部分

> 以下为本次 worktree 范围中尚未完成或需要重新设计的项。

### P9-UI — 待设计项

| ID | 组件 | 描述 | 优先 | 状态 |
|----|------|------|------|------|
| as-debug-output | mt/as | --debug 逐指令可视化/--stats | 🟢 低 | 待设计 |
| ld-tui-map | mt/ld | --map-tui TUI 链接映射/--why 符号溯源 | 🟢 低 | 待设计 |
| msysctl-upgrade | meuos-sysroot | tree/diff/--json 升级 | 🟢 低 | 待设计 |
| tool-integration | 跨组件 | 共享环境上下文（libmeuosenv/meuos_env.h） | 🟡 中 | 待设计 |
| meow-auto-diag | meow+ld | 构建失败时自动调用 ld --why 诊断 | 🟢 低 | 待设计 |
| post-check-hooks | meow | meow.yaml post_check 钩子 + mt-info 集成 | 🟢 低 | 待设计 |
| json-pipeline | 跨组件 | 统一 JSON lines 管道协议 | 🟢 低 | 待设计 |

### P5-meow — 重设计项（不照搬 autoconf/make 语法）

| ID | 类别 | 描述 | 状态 | 说明 |
|----|------|------|------|------|
| meow-template-subst | autoconf | 模板替换 | 🔄 重设计 | 不做 `@VAR@`（autoconf 遗留），用 YAML 原生表达式 |
| meow-wildcard | make | 文件列表通配 | 🔄 重设计 | 不做 `$(wildcard)`，用 `files('src/*.c')` |
| meow-conditional | make | 条件语句 | 🔄 重设计 | 不做 `ifeq`/`ifdef`，用 `when: ARCH == "aarch64"` |
| meow-vpath | make | 出源码构建 | 🔄 重设计 | meow 默认出源码构建（`build/<pkg>/`），不用额外抽象 |
| meow-pkg-config | pkg-config | `.pc` 文件查询 | 🔄 重设计 | 不做 `.pc` 解析，meow YAML 原生元数据或 `meow install` 数据库查询 |
| meowctl | meow | 配置界面 | 🔄 重设计 | 不做 `./configure` 克隆，meow 原生 `meow config` 查看检测结果 |

### 明确不做的项

| ID | 组件 | 描述 | 状态 |
|----|------|------|------|
| meow-libtool | meow | 共享/静态库管理 | ❌ 不做 `.la` 文件 |
| meow-native-shell | meow | 原生 shell 替代 | ⛔ 不在本次 worktree 范围（阻塞于 msh） |

### 待启动的子项目（不在本次 worktree 范围）

| 项目 | 状态 | 说明 |
|------|------|------|
| meuos-buildtools (m4/bison/flex/gperf) | ⏳ 待启动 | Phase 6 |
| meuos-utils (coreutils/diffutils/findutils) | ⏳ 待启动 | Phase 7 |
| meuos-shell (msh) | ⏳ 待启动 | Phase 7 |
| m++ C++ 前端 | ⏳ 待启动 | 阶段 A（libmcc 分离）已完成 |

---

## 架构完备性矩阵（最新状态）

| 准则 | x86_64 | aarch64 | riscv64 | i386 | loongarch64 | arm |
|------|--------|---------|---------|------|-------------|-----|
| mcc 后端 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 运行时 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/as | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/ld | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 全量构建 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| qemu 运行时验证 | ✅ 完整 | ✅ 完整 | ✅ hello/atomic/setjmp（线程跳过 qemu-user 限制） | ⚠️ qemu system | ✅ 完整 | ✅ 完整 |
| TLS 端到端 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| self-rebuild | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 动态链接 | ✅ ld.so 端到端 | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| DWARF 调试信息 | ✅ 行号表 | ✅ 行号表 | ✅ 行号表 | ✅ 行号表 | ✅ 行号表 | ✅ 行号表 |

---

## 待修复的 Bug（当前 worktree 发现）

（暂无）

---

## 已解决的问题

| ID | 组件 | 描述 | 修复 |
|----|------|------|------|
| bug-ld-pie-dynamic | mt/ld + mcc | mcc -pie 缺少 .dynamic 节区 | `2bf4f7a` |
| riscv64-qemu-thread | riscv64 | QEMU thread_cpu 全局变量（env qemu 10.1.0 修复） | 🟢 |
| check-sysroot-static-path | check-sysroot-static | temp 目录路径（已用 abspath 绕过） | 🟢 |
