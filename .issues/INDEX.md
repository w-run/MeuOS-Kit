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
   - GCC/Clang `__builtin_*` 映射（`__builtin_expect` → `if(__builtin_expect(...))` 等，待实现 D4-5）
   - GCC `__attribute__` 映射（`packed`、`aligned`、`section`、`weak`、`noreturn` 等，待实现 D4-3）
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
├── gcc/            # GCC 扩展兼容（待实现）
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

## 关键路径（p0-blockers）

| ID | 组件 | 架构 | 描述 | 状态 | 实施情况 |
|----|------|------|------|------|---------|
| bug-riscv64-emit | mcc backend | riscv64 | `riscv64_emit.c:664 assert(isreg(rb))` — emit 条件分支 slot/const 未 reg 降级 | 🟢 | `636f143` + `f6748e9` |
| bug-loong64-emit | mcc backend | loongarch64 | `loongarch64_emit.c:514 assert(isreg(rb))` — 同上模式 | 🟢 | `636f143` + `f6748e9` |
| bug-arm-isel | mcc backend | arm | `arm_isel.c: slot %(null) IR 诊断阻塞 self-rebuild` | 🟢 | `f6748e9` |
| bug-i386-tls | mcc isel/sema | i386 | TLS 模型选择：非静态 `_Thread_local` 发出 IE 而非 LE | 🟢 | `833c9b3` |
| bug-loong64-tls-reloc | mt/ld | loongarch64 | TLS LE 重定位 `.tdata` 初始值损坏 | 🔴 | `253de83` 部分分析，需 loongarch64 环境调试 |
| bug-loong64-tls-errno | meuos-libc | loongarch64 | TLS errno 回退到 `static int`（非线程安全） | 🔴 | 阻塞于 bug-loong64-tls-reloc |

## 优先级 1（P1-core — 功能完善，需要实现）

| ID | 组件 | 项目 | 描述 | 状态 | 实施情况 |
|----|------|------|------|------|---------|
| meow-dag-dedup | meow | DAG 去重 | `-jN` 并行构建时间接依赖重复执行（`.todo/dag-dedup.md`） | 🟢 | `9c20ae2` |
| mcc-atomic-voidptr | mcc sema | `_Atomic int*` → `void*` | C 6.3.2.3p1 限定对象指针应可转 `void*`（chibicc 测试报） | 🟢 | `03e8618` 验证通过 |
| mcc-riscv64-qemu | mcc | riscv64 qemu 门禁 | 完整 qemu 运行时门禁（当前仅 exit=42） | ⏳ | 待实现 |
| mcc-loong64-qemu | mcc+libc | loongarch64 qemu 门禁 | 完整 qemu 运行时门禁（当前仅 exit=42） | ⏳ | 待实现 |
| mcc-i386-tls-e2e | mcc+libc | i386 TLS e2e | bug-i386-tls 修复后的 TLS 端到端验证 | ⏳ | 待实现 |
| mcc-i386-tls-doc | mcc(i386) | `gd-tls.md` 文档 | 被 3 个文件引用但文件不存在 | 🟢 | `03e8618` |
| ld-shared | mt/ld | `-shared` 输出 `ET_DYN` | 🟢 ET_DYN + PHDR/DYNAMIC + dynsym/dynstr/hash/dynamic 完整实现 | `a0822fe` 动态节区数据填充 |
| ld-pie | mt/ld | `--pie`/`--no-pie` 支持 | PIE 二进制输出（`ET_DYN` + `PT_INTERP` + 相对重定位） | 🟢 | `4ae63b1` |
| mcc-pic-verify | mcc | PIC 代码生成加固 | 全架构验证 `-fPIC` 输出（GOT/PLT/TLS GD 路径） | ⏳ | 待实现 |
| mcc-shared-mt | mcc driver | `-shared` mt/ld 集成 | 去掉 `-shared` 回退到 host cc 的限制 | 🟢 | `acce6c6` |
| ld-so | 新建 | ld.so 动态链接器 | ELF 加载、DT_NEEDED 图遍历、符号解析、重定位应用、延迟绑定、TLS init | 🟡 | `525ab54` 骨架实现（x86_64、ELF 加载+符号解析+重定位+裸 syscall） |
| libc-dl | meuos-libc | `dlfcn.h` + `dl*` 实现 | `dlopen`/`dlsym`/`dlclose`/`dlerror` | 🟡 | `ef0edd0` 头文件+框架（阶段1），阶段2需要完整 ELF 加载实现 |
| mcc-dwarf | mcc | DWARF 调试信息 | `-g` 生成 DWARF v5（`.debug_info`/`.abbrev`/`.line`/`.str`/`.loc`/`.ranges`），包含行号、变量、类型信息 | 🟢 | `a9a065c` 行号表(阶段1); .debug_info/abbrev/str 阶段2待实现 |
| as-dwarf | mt/as | DWARF 汇编伪指令 | `.loc`/`.file`/`.cfi_*` 支持 — **阻塞 mcc-dwarf**，无此 as 无法处理 `-g` 输出 | 🟢 | `a5f49c0` |
| ld-dwarf-merge | mt/ld | DWARF 节区合并 | 链接时合并 `.debug_*` 节区，生成 `.debug_line`/`.debug_info` 跨目标文件 | ⏳ | 待实现 |
| ld-build-id | mt/ld | `--build-id` | 生成 `.note.gnu.build-id`（FNV-1a hash，用于调试/构建标识） | 🟢 | 本 commit |
| ld-eh-frame-hdr | mt/ld | `--eh-frame-hdr` | 生成 `.eh_frame_hdr`（异常处理帧索引，gcc 异常必需） | 🟢 | 本 commit |
| ld-as-needed | mt/ld | `--as-needed` / `--no-as-needed` | 避免不必要的 DT_NEEDED 条目 | ⏳ | 待实现 |
| ld-start-group | mt/ld | `--start-group` / `--end-group` | 循环依赖库解析（`-Wl,--start-group -la -lb -lc --end-group`） | ⏳ | 待实现 |
| ld-whole-archive | mt/ld | `--whole-archive` / `--no-whole-archive` | 强制归档中所有目标文件链接（用于 plugin/init 段） | 🟢 | 本 commit |

## 优先级 2（P2-toolchain — 生态集成）

| ID | 组件 | 项目 | 描述 | 状态 | 实施情况 |
|----|------|------|------|------|---------|
| meow-native-shell | ⛔ | 原生 shell 替代 | 阻塞于 msh（不在本次 worktree 范围） | ⛔ | 不在范围 |
| mcc-msys-link | mcc driver | `.msys` + host linker | host cc 链接时自动提取 `.a` 到 temp | 🟡 | 部分实现（msys.c 已有基础） |
| ld-tls-dynamic | mt/ld | TLS 动态模型 | GD/LD 模型、`__tls_get_addr`（依赖 ld-shared） | ⏳ | 待实现 |
| ld-gc-sections | mt/ld | 死代码消除 | 未引用节区的裁剪。概念有用，实现应自己设计，不照搬 GNU `--gc-sections` 的复杂逻辑 | ⏳ | 待实现 |
| ld-linker-script | mt/ld | 链接布局控制 | ❌ **不做 GNU `.ld` 脚本解析**。需要时改为 YAML 格式描述节区布局（链接器内嵌或独立文件） | 🔄 重设计 | 待设计 |
| ld-print-map | mt/ld | 链接摘要输出 | ⚠️ 如需要，设计自己的简洁输出格式，不仿 GNU ld map 的 clunky 排版 | 🟡 | 待实现 |
| as-macro | mt/as | 宏/重复伪指令 | ⚠️ mcc 生成汇编的话，`.macro` 需求不大。保留但按需实现，不照搬 GAS 语法 | 🟡 | 待实现 |
| ld-defsym | mt/ld | `--defsym` | ❓ 链接时定义符号——MeuOS 场景下是否真正需要？暂不实现 | ❓ | 待定 |
| ld-wrap | mt/ld | `--wrap` | ❓ GDB 风格的符号包装。暂不实现 | ❓ | 待定 |
| ld-version-script | mt/ld | 符号版本控制 | ❓ 复杂的版本脚本。暂不实现 | ❓ | 待定 |
| ld-no-undefined | mt/ld | `--no-undefined` | ⚠️ 有用但设计自己的行为——默认报错还是可配置？ | 🟡 | 待实现 |
| ld-cref | mt/ld | `--cref` | ❓ GNU 交叉引用表格式。暂不实现 | ❓ | 待定 |
| ld-compress-debug | mt/ld | DWARF 压缩 | ⚠️ 可使用通用 `--compress` 而非照搬 `--compress-debug-sections` 命名 | 🟡 | 待实现 |
| as-full-isa | mt/as | 全架构指令完整覆盖 | 各架构缺的少用指令补全 | ⏳ | 待实现 |
| as-cond | mt/as | 条件汇编 | ⚠️ `.if`/`.ifdef` — GAS 语法。如果 mcc 生成汇编则需求不大；但如果要能汇编手写 `.S` 则有用。按需实现 | 🟡 | 待实现 |
| as-align | mt/as | 对齐/填充伪指令 | `.balign`/`.p2align` — 通用汇编概念，不是 GAS 特有 | 🟡 | 待实现 |
| as-section | mt/as | 节区控制伪指令 | `.pushsection`/`.popsection` — 通用概念，用于 gcc asm 属性 | 🟡 | 待实现 |
| as-equ | mt/as | 常量/符号定义 | `.equ`/`.set` — 通用概念 | 🟢 | 已实现 |
| as-diag | mt/as | 汇编诊断 | `.abort`/`.error`/`.warning` | 🟢 | 已实现 |
| ld-linker-script | mt/ld | 链接脚本支持 | `.ld` 脚本解析与布局控制（替代 `-T` 占位） | ⏳ | 待实现 |
| ld-print-map | mt/ld | `--print-map` | 链接映射输出 | ⏳ | 待实现 |
| ld-defsym | mt/ld | `--defsym` | 链接时定义符号（`--defsym=foo=bar`） | 🟡 | 待实现 |
| ld-wrap | mt/ld | `--wrap` | 符号包装（`--wrap=malloc` → `__wrap_malloc`）用于测试/mock | 🟡 | 待实现 |
| ld-version-script | mt/ld | `--version-script` | 符号版本控制/导出限制（`.map` / `.ver` 文件） | 🟡 | 待实现 |
| ld-no-undefined | mt/ld | `--no-undefined` | 未定义符号时报错（`-z undefs` / `--no-undefined`） | 🟡 | 待实现 |
| ld-cref | mt/ld | `--cref` | 交叉引用表输出 | 🟢 | 已实现 |
| ld-compress-debug | mt/ld | `--compress-debug-sections` | DWARF 节区压缩（zlib/zstd） | 🟢 | 已实现 |
| meow-multi-dir | meow | 多目录构建 | 跨目录包依赖的 YAML 配方 | ⏳ | 待实现 |
| tool-binary | 新建 | 统一二进制分析工具 | **不做 size/strings/addr2line/ldd 各自一个工具**。设计一个 `mt-info` 或集成到 `objdump`/`readelf` 中，通过 subcommand 提供多种分析能力 | 🔄 重设计 | 待设计 |

## 优先级 3（P3-libc — libc 标准接口完备）

> C 库标准接口（ISO C11 + POSIX）的完整实现。当前核心 libc 已有基础框架，
> 以下为尚未实现的或仅 stub 的接口族。

| ID | 模块 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| libc-math | `<math.h>` | 数学库：`sin`/`cos`/`sqrt`/`log`/`exp`/`pow` 等 IEEE 754 浮点函数 | 🔴 高 | 待实现 |
| libc-printf | `<stdio.h>` | 完整 `printf`/`scanf` 格式覆盖（浮点、`%n`、宽字符、长 double） | 🔴 高 | 待实现 |
| libc-time | `<time.h>` | 完整 `strftime`、时区处理、`clock_gettime` POSIX 扩展 | 🟡 中 | 待实现 |
| libc-pthread | `<pthread.h>` | rwlock/barrier/spinlock/cleanup handler 完整覆盖 | 🟡 中 | 待实现 |
| libc-str | `<string.h>` | `strerror_r` 线程安全变体、`strcoll`/`strxfrm` locale 感知 | 🟢 低 | 待实现 |
| libc-wchar | `<wchar.h>` | 宽字符 I/O、宽字符 `printf`/`scanf`、wcsftime | 🟢 低 | 待实现 |
| libc-locale | `<locale.h>` | locale 感知函数（`setlocale` 当前 stub） | 🟢 低 | 待实现 |
| libc-complex | `<complex.h>` | 复数算术和数学函数 | 🟢 低 | 待实现 |
| libc-socket | POSIX 网络 | `<sys/socket.h>`、`<netdb.h>`、`<netinet/in.h>`、`<arpa/inet.h>` | 🟡 中 | 待实现 |
| libc-regex | POSIX 正则 | `<regex.h>` — `regcomp`/`regexec`/`regerror`/`regfree` | 🟡 中 | 待实现 |
| libc-termios | POSIX 终端 | `<termios.h>`、`<sys/ioctl.h>` | 🟢 低 | 待实现 |
| libc-glob | POSIX glob | `<glob.h>`、`<fnmatch.h>` 模式匹配 | 🟡 中 | 待实现 |
| libc-syslog | POSIX 环境 | `<syslog.h>`、`<utmpx.h>` | 🟢 低 | 待实现 |
| libc-atomic | `<stdatomic.h>` | C11 atomic 的完整操作集（`atomic_compare_exchange_*` 变体等） | 🟡 中 | 待实现 |
| libc-threads | `<threads.h>` | C11 thread 完整接口（`tss_*`、`call_once` 等） | 🟡 中 | 待实现 |

## 优先级 4（P4-devexp — 开发者体验）

> 编译器质量和开发者体验优化。这些项不影响功能正确性，但影响日常使用体验。

| ID | 组件 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| mcc-diagnostics | mcc | 诊断质量 | 带源位置和 caret（`^`）的错误消息。这是 Clang 推广的好设计，非 GNU 包袱 | 🟡 | 待实现 |
| mcc-warnings | mcc | 警告体系 | ⚠️ `-Wall`/`-Wextra` 是 GCC 命名约定。我们应该设计自己的警告体系（`--warn=all`/`--warn=extra` 或 `-W` 风格但自己定义哪些组别） | 🔄 重设计 | 待设计 |
| mcc-attributes | mcc compat | `__attribute__` | GCC 属性语法。按设计原则应走 compat 映射层，核心不直接处理 | 🟡 | 待实现 |
| mcc-pragma | mcc compat | `#pragma` | GCC/Clang pragma。同样走 compat 映射层 | 🟡 | 待实现 |
| mcc-builtins | mcc compat | `__builtin_*` | GCC/Clang 内建函数。同样走 compat 映射层（`__builtin_expect` → 宏等） | 🟢 | 待实现 |
| mcc-generic | mcc | `_Generic` 完整 C11 匹配规则（含 qualified type 分派） | 🟡 中 | 待实现 |
| as-errors | mt/as | 错误消息行号/列号 | 🟢 低 | 待实现 |
| ld-errors | mt/ld | 未定义符号的友好诊断（列出候选目标文件） | 🟢 低 | 待实现 |
| community-tests | 全项目 | 社区测试套件覆盖率（chibicc → C99/C11/C23 全量通过） | 🟡 中 | 待实现 |
| meow-lint | meow | 配方语法检查器（`meow lint`） | 🟢 低 | 待实现 |
| ci-pipeline | 全项目 | CI/CD 流水线（GitHub Actions + qemu-user 跨架构回归） | 🟡 中 | 待实现 |

## 优先级 5（P5-meow — meow 构建系统完备）

> meow 目标是替代 make + autoconf + libtool + pkg-config。当前约 40% Make、
> 20% autoconf、0% pkg-config/libtool。以下按影响排序。

| ID | 类别 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| meow-template-subst | autoconf | **模板替换** | ⚠️ 不做 `@VAR@`（autoconf 遗留）。需要时用 YAML 原生表达式或 meow 自己的模板语法 | 🔄 重设计 | 待设计 |
| meow-wildcard | make | **文件列表通配** | ⚠️ 不做 `$(wildcard)`（GNU make 语法）。用 YAML 原生匹配或 meow 自己的函数（`files('src/*.c')`） | 🔄 重设计 | 待设计 |
| meow-check-library | autoconf | **`check_library` / `check_link`** | 链接测试检测 `-lz`、`-lpthread`。概念本身没问题——autoconf 的 probe 机制是合理的设计。实现时不照搬 autoconf 语法即可 | 🔴 高 | 待实现 |
| meow-conditional | make | **条件语句** | ⚠️ 不做 `ifeq`/`ifdef`（GNU make 语法）。YAML 条件语句（`when: ARCH == "aarch64"`）或类似 DSL | 🔄 重设计 | 待设计 |
| meow-type-size | autoconf | **`check_type_size`** | 检测 `sizeof(time_t)`。autoconf 的这个概念合理，不照搬其实现。meow 应有自己的 `type_size()` probe 函数 | 🟡 中 | 待实现 |
| meow-probe-cache | autoconf | **Probe 缓存** | 缓存编译测试结果（`config.cache` 等价物）。概念好，实现不照搬 autoconf 的烦人缓存格式 | 🟡 中 | 待实现 |
| meow-vpath | make | **出源码构建** | ⚠️ 不做 `$(srcdir)`（GNU make VPATH）。meow 默认出源码构建（`build/<pkg>/`），不用额外抽象 | 🔄 重设计 | 待设计 |
| meow-subdirs | make | **多目录构建** | 跨目录包依赖的 YAML 配方。不是 GNU `AC_CONFIG_SUBDIRS`，是 meow 自己的依赖模型 | 🟡 中 | 待实现 |
| meow-pkg-config | pkg-config | **`.pc` 文件查询** | ⚠️ 不做 `.pc` 解析（freedesktop.org 格式）。meow 应有自己的包元数据格式（YAML 原生），或只通过 `meow install` 注册的数据库查询 | 🔄 重设计 | 待设计 |
| meow-libtool | libtool | **共享/静态库管理** | ❌ 不做 `.la` 文件。libtool 是历史遗留，mt/ld 直接管理库格式，meow 只需知道库路径 | ❌ 不做 | 不做 |
| meow-dag | meow | DAG 去重 | `-jN` 间接依赖重复执行。纯 meow 自己设计，无历史包袱 | ⏳ | `9c20ae2` |
| meowctl | meow | 配置界面 | ⚠️ 不做 `./configure` 克隆。meow 自动检测，真正的配置界面（如 `meow config` 查看检测结果）应该简洁 | 🔄 重设计 | 待设计 |

## 优先级 7（P7-subarch — 子架构与 CPU 特性支持）

> 当前所有 6 架构都是"每个架构一个基准 ISA"模型，无子架构区分、无特性标志系统、
> 无 `-march=native`。对于真实世界软件，子架构感知必不可少。

### 现状

| 特性 | 状态 | 说明 |
|------|------|------|
| `Target.features` / 特性标志位 | ❌ 不存在 | Target 结构体无 capability 字段，无 ISA 级别概念 |
| `-march`/`-mcpu`/`-mfpu` | ⚠️ 仅 ARM | 仅影响预处理器宏，**不影响代码生成** |
| `-mfloat-abi` | ⚠️ 仅 ARM | hard 还是 soft 影响 ABI |
| `-mtune` | ❌ 不存在 | |
| `-march=native` | ❌ 不存在 | 无 CPUID、无 `/proc/cpuinfo` 检测 |
| **mt/as 指令门控** | ❌ 不存在 | 编码器解码即支持，无特性假设检查 |

### 各架构的缺口

| 架构 | 子架构/特性 | 现状 | 影响 |
|------|------------|------|------|
| **x86_64** | ISA 级别 v1/v2/v3/v4 | ❌ 只发标量 SSE、无 AVX/AVX2/AVX-512 | 无法利用现代 CPU 特性；某些需要 SSE4.2/AVX 的软件无法编译 |
| **x86_64** | `-mno-sse` / `-mno-mmx` | ❌ 不存在 | 某些嵌入式/内核编译需要关 SSE |
| **aarch64** | SVE/SVE2/RME/FP16 | ❌ 只发基础 AArch64 指令 | 无法优化向量化代码 |
| **riscv64** | 扩展选择（F/D/C/V/Zb*/Zk*） | ❌ 隐含 F/D/A，无选择机制 | 无法构建无浮点或压缩指令的目标 |
| **riscv64** | `-mabi=lp64` vs `lp64d` vs `ilp32` | ❌ 只有 LP64D | 无法构建无浮点 binary |
| **i386** | 486/586/686/Pentium 变体 | ❌ 全部映射到同一 Target | 387 vs SSE2 差异未利用 |
| **loongarch64** | LSX/LASX/LVZ | ❌ 只发基础 LA64 | 无法利用 SIMD |
| **arm** | ARMv6/v7/v8 实际代码差异 | ❌ 单一 `armv7` 后端 | `-march=armv8-a` 仍发 ARMv7 指令 |

### 子任务

| ID | 描述 | 优先 | 实施情况 |
|----|------|------|---------|
| target-features | **`Target.features` 设计**：在 Target 中添加 `uint64_t features` 位图 + `const char *features_desc[]`。定义架构无关的公共特性位和架构特有的特性位 | 🔴 高 | 待实现 |
| march-generic | **`-march=` 解析通用化**：从仅 ARM 推广到全架构。x86_64 解析 `native`/`x86-64`/`x86-64-v2`/`v3`/`v4`；riscv64 解析 `rv64gc`/`rv64imafdc`；aarch64 解析 `armv8-a`/`armv8.2-a` 等 | 🔴 高 | 待实现 |
| x86-isa-levels | **x86_64 ISA 级别门控**：实现 `-march=x86-64-v2`/`v3`/`v4` 代码生成差异。至少：v2 启用 SSE4.2+POPCNT，v3 启用 AVX2+BMI2，v4 启用 AVX-512 | 🔴 高 | 待实现 |
| riscv-extensions | **riscv64 扩展选择**：实现 `-march=rv64imafdc` 解析，根据扩展集发射指令。`-mabi=lp64d`/`lp64`/`ilp32d`/`ilp32` | 🟡 中 | 待实现 |
| arm-multiver | **arm 多版本后端**：根据 `-march` 切换 ARMv6/v7/v8 指令选择器和发射器差异（Thumb/ARM 模式、DMB 变体等） | 🟡 中 | 待实现 |
| aarch64-ext | **aarch64 架构扩展**：FEAT_FP16/FEAT_RDM/FEAT_JSCVT 等特性位与代码生成 | 🟢 低 | 待实现 |
| march-native | **`-march=native`**：通过 CPUID（x86）或 `/proc/cpuinfo` 查询宿主机特性并设置 Target.features | 🟡 中 | 待实现 |
| as-isa-gating | **mt/as 指令门控**：编码器根据 insn 要求的特性位进行验证，不支持的指令报错而非默默生成 | 🟡 中 | 待实现 |
| i386-variants | **i386 变体区分**：486/586/686 在 cmpxchg/CMPXCHG8B/FPU 存在性上的差异映射到特性位 | 🟢 低 | 待实现 |

### 设计提案

```
// Target 特性位设计（草案）
enum target_feature {
    TF_FPU       = 1ULL << 0,   // 硬浮点（架构特定的 FPR）
    TF_SOFT_FLOAT = 1ULL << 1,  // 软浮点（无 FPR，用 GPR 模拟）
    TF_ATOMICS    = 1ULL << 2,  // 硬件原子操作（lock/lr/sc）
    TF_SSE        = 1ULL << 3,  // x86 SSE
    TF_SSE2       = 1ULL << 4,  // x86 SSE2
    TF_AVX        = 1ULL << 5,  // x86 AVX
    TF_AVX2       = 1ULL << 6,  // x86 AVX2
    TF_AVX512F    = 1ULL << 7,  // x86 AVX-512 Foundation
    TF_POPCNT     = 1ULL << 8,
    TF_BMI        = 1ULL << 9,  // x86 BMI1/BMI2
    TF_AARCH64_FP16 = 1ULL << 10, // aarch64 FEAT_FP16
    TF_AARCH64_SVE  = 1ULL << 11, // aarch64 SVE
    TF_RV_F       = 1ULL << 12, // riscv F 扩展
    TF_RV_D       = 1ULL << 13, // riscv D 扩展
    TF_RV_C       = 1ULL << 14, // riscv C 压缩指令
    TF_RV_V       = 1ULL << 15, // riscv V 向量
    TF_ARM_THUMB  = 1ULL << 16, // arm Thumb 模式
    TF_ARM_VFP    = 1ULL << 17, // arm VFP
    TF_ARM_NEON   = 1ULL << 18, // arm NEON
};
```

## 优先级 6（P6-c23 — C23 标准边缘情况）

> mcc 已实现 AGENTS.md §2.2 所列 C23 主要特性。以下为剩余边缘情况。

| ID | 描述 | 状态 | 实施情况 |
|----|------|------|---------|
| c23-constexpr | `constexpr` 初始化规则（运行时 vs 编译期求值边界） | ⏳ | 待实现 |
| c23-attributes | `[[]]` 属性语法全位置覆盖（声明/类型/语句/标签） | ⏳ | 待实现 |
| c23-bool | `bool`/`true`/`false` 关键字 vs `<stdbool.h>` 宏兼容 | ⏳ | 待实现 |
| c23-embed | `#embed` 边界情况（大文件/`limit(N)`/`prefix`/`suffix`/`if_empty`） | ⏳ | 待实现 |
| c23-typeof | `typeof`/`typeof_unqual` 在复杂声明中的应用 | ⏳ | 待实现 |

## 优先级 3（P3-libc — 社区测试兼容性，低优先级）

| ID | 组件 | 描述 | 状态 | 实施情况 |
|----|------|------|------|---------|
| mcc-float-suffix | mcc lexer | C23 `100f` float 后缀支持 | ⏳ | 待实现 |
| mcc-uint64-max | mcc sema | UINT64_MAX 字面量类型回退到 `unsigned long long` | ⏳ | 待实现 |
| mcc-macro-redef | mcc preproc | 宏定义相同 token 序列允许重定义（C 6.10.3p2） | ⏳ | 待实现 |
| mcc-line-num | mcc preproc | `__LINE__` 偏移 1 | ⏳ | 待实现 |
| mcc-common-sym | mcc sema | common/tentative-definition 合并行为 | ⏳ | 待实现 |
| mcc-unicode | mcc lexer | Unicode/C11 标识符（$/UCN/UTF-8） | ⏳ | 待实现 |
| mcc-va-end | mcc | `__builtin_va_end` 类型检查时机（宏定义处而非使用处） | ⏳ | 待实现 |

---

## 架构完备性矩阵

| 准则 | x86_64 | aarch64 | riscv64 | i386 | loongarch64 | arm |
|------|--------|---------|---------|------|-------------|-----|
| mcc 后端 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 运行时 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/as | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/ld | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 全量构建 | ✅ | ✅ | ✅ bug-riscv64-emit | ✅ | ✅ bug-loong64-emit/bug-loong64-tls-reloc | ✅ |
| qemu 运行时验证 | ✅ 完整 | ✅ 完整 | ⚠️ exit=42 | ⚠️ qemu system | ⚠️ exit=42 | ✅ 完整 |
| TLS 端到端 | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ❌ bug-loong64-tls-reloc/bug-loong64-tls-errno | ✅ |
| self-rebuild | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ bug-loong64-emit | ❌ bug-arm-isel |
| 动态链接 | 🔄 ld-shared...ld-so | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| DWARF 调试信息 | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| mcc 诊断/警告 | 🔄 mcc-diagnostics...mcc-generic | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| libc 完整 POSIX | 🔄 libc-math...libc-threads | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| mt/as 完整指令 | 🔄 as-macro+as-full-isa+as-cond+as-align+as-section+as-equ+as-diag | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| mt/ld 链接器特性 | 🔄 ld-gc-sections+ld-linker-script(重构)+ld-print-map(重构)+ld-start-group+ld-whole-archive+ld-build-id+ld-eh-frame-hdr+ld-as-needed | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| 工具链辅助工具 | 🔄 tool-binary（统一二进制分析工具） | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| C23 边缘情况 | 🔄 c23-xxxx | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| CI/CD 跨架构 | 🔄 ci-pipeline | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |

---

## 总体范围层级

```
p0-blockers ── 6 个阻塞 bug（架构独有，必须修才能声称架构可用）
  ├── bug-riscv64-emit-bug-arm-isel: emit/isel bug（riscv64/loongarch64/arm）
  └── bug-i386-tls-bug-loong64-tls-errno: TLS 缺口（i386/loongarch64）

p1-core ── 核心功能完善（编译器+工具链+运行时质量）
  ├── meow-dag-dedup...mcc-i386-tls-doc:  现有组件完善（meow DAG/mcc/libc）
  ├── ld-shared...ld-so:  p6-dynamic-link（mt/ld -shared → ld.so → dlopen）
  └── mcc-dwarf+ld-dwarf-merge: p8-dwarf（行号表 → 类型 DIE → 合并）

p2-toolchain ── 工具链生态完善（汇编/链接器特性）
  ├── mt/ld: --gc-sections, 链接脚本, --print-map
  ├── mt/as: 宏/重复, 全架构指令覆盖, ARM host_cc 检测
  └── meow: 多目录构建, 原生 shell

p3-libc ── libc 标准接口完备（ISO C11 + POSIX 全量）
  ├── 高: math.h, 完整 printf/scanf
  ├── 中: pthread, socket, regex, glob, atomic, thread, time
  └── 低: wchar, locale, complex, termios, syslog

p4-devexp ── 开发者体验（编译器质量、诊断、CI）
  ├── mcc: 诊断 caret、警告体系(重设计，不用 GCC 命名)、__attribute__(compat)、#pragma(compat)、__builtin_(compat)、_Generic
  ├── mcc: 社区测试覆盖（chibicc 全过）
  └── CI/CD: GitHub Actions + qemu-user 跨架构回归

p5-meow ── meow 构建系统完备
  ├── 重设计: 模板替换(YAML原生)、wildcard(meow函数)、条件语句(meow DSL)、VPATH(默认out-of-tree)、.pc(meow元数据)
  ├── ❌ 不做: .la 文件(libtool)、./configure 克隆
  ├── 合理保留: check_library/check_link/check_type_size(概念好，重新实现)、probe缓存
  └── meowctl(配置查看界面)

p6-c23 ── C23 标准边缘情况
  └── constexpr, 属性语法, bool 关键字, #embed, typeof

p7-subarch ── 子架构与 CPU 特性支持（当前最薄弱的环节）
  ├── 高: Target.features 设计, -march 通用化, x86_64 ISA 级别门控
  ├── 中: riscv64 扩展选择, arm 多版本, mt/as 指令门控
  └── 低: aarch64 扩展, i386 变体

p8-meow ── meow 构建系统完备（替代 make+autoconf+libtool+pkg-config）
  └── ...（见 p5-meow 表）

### 跨域设计项（影响多个组件）

| ID | 主题 | 描述 | 涉及组件 | 优先 | 实施情况 |
|----|------|------|---------|------|---------|
| specs-default | **`--specs=meuos` 默认化** | `MEUOS_SYSROOT` 设置时自动隐含 `--specs=meuos`，不再需要显式传入。新增 `--specs=host` 切回宿主模式 | 🟢 | `42a53ed` |
| meow-auto-config | **meow 自动决策编译参数** | `meow build <pkg> --target=<triplet>` 自动解析 triple → arch/abi/float/subarch, 自动设 CC/CFLAGS/LDFLAGS, 自动跑 probe 生成 config.h | meow + mcc | 🔴 高 | 待实现 |
| triple-format | **MeuOS triple 格式设计** | 定义 `<arch>[-subarch][-vendor][-os][-abi]` 格式。vendor=`meuos` 隐含 MeuOS 默认行为；os=`meuos-next` 为未来原生环境。见下方详细设计 | meow + mcc + mt | 🔴 高 | 设计已定稿，待实现 |
| triple-lib | **共享 triple 解析库** | meow 和 mcc 共享同一套 triple 解析逻辑，避免两处分叉。提取为 `libtriple` 或共用头文件 | meow + mcc | 🔴 高 | 待实现 |
| triple-abi-map | **Triple → ABI 自动映射** | `--target=<triplet>` 精确提取 ABI/lp64/float 信息并选择对应 Target/ABI 降级 | mcc sema | 🟡 中 | 待实现 |
| meow-zero-args | **meow 零参数构建** | `meow build` 无参数时自动嗅探当前架构/sysroot/源码，生成完整构建环境；只有需要定制时才传入参数 | meow | 🔴 高 | 待实现 |

### Triple 格式设计

> 只传主架构（`aarch64`）无法确定子架构。必须用完整 triple 承载足够信息。

#### 格式

```
<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
```

| 字段 | 说明 | 示例值 |
|------|------|--------|
| arch | 主架构（必选） | `x86_64`, `aarch64`, `riscv64`, `i386`, `loongarch64`, `arm` |
| subarch | 子架构/ISA 级别（可选） | `v2`, `v3`, `v4`, `armv7`, `rv64gc` |
| vendor | 供应商（可选，默认未知） | `meuos`, `unknown`, `pc` |
| os | 操作系统（可选） | `linux`, `meuos-next`, `none` |
| abi | ABI 变体（可选） | `gnu`, `gnueabihf`, `lp64d`, `lp64`, `ilp32` |

#### 当前阶段（Linux 宿主）

```
x86_64-meuos-linux          → MeuOS Kit on Linux, x86_64 默认
aarch64-meuos-linux         → 交叉编译 aarch64 MeuOS
x86_64-v3-meuos-linux       → x86_64 ISA v3（启用 AVX2+BMI2）
armv7-meuos-linux-gnueabihf → ARMv7 hard-float
riscv64-meuos-linux-lp64d   → RISC-V LP64D
```

vendor=`meuos` 的含义：
- 默认 `--specs=meuos`（不需要显式传）
- 默认使用 `$MEUOS_SYSROOT`
- 默认使用 mt/as + mt/ld
- 默认链接 `libc-meuos.a`

#### 未来阶段（原生 MeuOS Next）

```
x86_64-meuos-next           → 原生 MeuOS Next 环境
aarch64-meuos-next          → 交叉编译 aarch64 MeuOS Next
```

os=`meuos-next` 的含义：
- MeuOS Next 自定义内核 ABI（非 Linux syscall）
- MeuOS Next 原生 libc
- MeuOS Next 原生 ELF/格式变体

#### meow 的 triple 自动推导

```sh
meow build                   # 自动检测宿主 → x86_64-meuos-linux
meow build --target=aarch64  # 简写 → meow 补全为 aarch64-meuos-linux
meow build --target=aarch64-meuos-linux  # 全称
```

triple 解析器在 `triple-lib` 中实现，meow 和 mcc 共享同一份代码。

### meow 用户体验设计原则

> meow = autoconf + make + libtool + pkg-config 集合体。
> 参考 Go 的设计哲学：零配置、快、跨编译简单、输出干净，但不照搬命令——要符合 C 项目和 MeuOS 自己的习惯。

#### 参考的设计哲学

| Go 的设计思路 | 对我们意味着什么 |
|--------------|---------------|
| `go build` 零配置 | `meow build` 不传参数就能干活——自动检测一切 |
| 交叉编译用环境变量/参数 | 延续已有的 `--target=triple`，不用另起炉灶 |
| 编译缓存 | 增量构建 + `~/.cache/meow/` |
| 默认只显示错误 | 编译器命令行默认隐藏，`--verbose` 展开 |
| 不污染源码目录 | 产物一律进 `build/<pkg>/`，要做 `-C <dir>` 也支持 |
| 快速增量 | DAG 去重 + mtime 追踪 |
| 不自带语言特性检查（go vet 是工具） | `meow lint` 仅做配方检查，编译器诊断是 mcc 的事 |
| `go generate` 代码生成 | `meow.yaml` 的 `generate:` 步骤 |
| `go test` 测试 | `make check` 是已有的习惯，meow 也可以封装 |

#### 命令风格原则

```
- 保持 C 项目的直觉（不像 Go 那样把语言和构建绑定）
- 延续 meow 已有的习惯（--target= / --sysroot= / -jN）
- 汉语友好的长选项名（--jobs 优于 -j 作唯一名）
- 分层的 subcommand 体系
```

#### meow CLI 体系

```sh
meow build [pkg] [options]          # 构建（默认当前目录包）
meow clean [pkg]                    # 清理
meow list                           # 列出可用包
meow install [pkg]                  # 安装到 sysroot
meow test [pkg]                     # 跑测试（相当于 make check）
meow lint                           # 配方语法检查
meow env                            # 打印检测到的构建环境
meow version                        # 打印版本
```

#### 默认行为（零参数）
...

```sh
cd <project>       # 进入有 meow.yaml 的目录（或 meow.yaml 定义了根包）
meow build          # ← 这就是全部

# meow 自动做：
#   1. 检测宿主编译器（mcc on MEUOS_SYSROOT / fallback host cc）
#   2. 检测 MEUOS_SYSROOT（环境变量 / 默认路径 / sysroot(架构).msys）
#   3. 检测宿主架构 → 设 ARCH
#   4. 跑 probe（头文件检查/函数检查/类型大小）→ 生成 config.h
#   5. 解析 meow.yaml → 构建依赖图 → 执行
#   6. 安装到 $MEUOS_SYSROOT
```

这等价于 gcc 世界的 `./configure && make && make install`，一步到位。

#### 覆写参数（仅需定制时传入）

```sh
# 交叉编译
meow build --target=aarch64                    # 目标架构
meow build bzip2 --target=riscv64              # 指定包+架构

# 自定义 sysroot
meow build --sysroot=/custom/sysroot
meow build --sysroot=/tmp/sysroot-aarch64.msys  # .msys 文件也行

# 构建目录（out-of-tree）
meow build bzip2 -C build/bzip2                # srcdir != builddir
meow build -C build/all                        # 全局 out-of-tree

# 资源控制
meow build --jobs=8                            # 并行（默认 NCPU）
meow build --mem-limit=4G                      # 内存上限
meow build --quiet                             # 静默模式

# 诊断
meow build --verbose                           # 详细日志
meow build --dry-run                           # 只打印不执行
meow lint                                      # 配方语法检查
```

#### 设计含义

| 能力 | 当前状态 | 目标 |
|------|---------|------|
| `meow build` 无参数 | ❌ 必须指定包名 | ✅ 默认包（根 meow.yaml 定义） |
| 架构自动检测 | ❌ 必须 `--arch=` | ✅ 自动检测宿主，`--target=` 覆盖 |
| sysroot 自动检测 | ❌ 必须环境变量 | ✅ `$MEUOS_SYSROOT` / `./sysroot` / `./*.msys` |
| probe 自动跑 | ❌ 必须 recipe 显式调用 | ✅ 构建前自动跑所有声明的 probe |
| config.h 自动生成 | ❌ 不存在 | ✅ probe 结果写入 `build/config.h` |
| out-of-tree (`-C`) | ❌ 不存在 | ✅ 支持 VPATH 风格 |
| 资源控制 | ❌ 只有 `-jN` | ✅ `--jobs=` / `--mem-limit=` |
| install 自动完成 | ❌ 需要显式 target | ✅ `meow build` 默认包含 install |

## p6-dynamic-link — 子任务分解

### 范围

动态链接涉及 mt/ld、mcc、libc、ld.so 四个组件的联动改动。依赖关系线性：

```
mt/ld -shared → mcc -shared 集成 → ld.so → libc dlopen → 全架构适配
```

### 子任务详情

#### ld-shared: mt/ld `-shared` 输出（ET_DYN）

**文件**: `projects/meuos-toolchain/src/ld/`

**改动**:
- `main.c`: 添加 `--shared` CLI 解析
- `ld.h`: `mt_ld_link()` 增加输出类型参数
- `link.c`: 核心改动：
  - 生成 `.dynsym`（`SHT_DYNSYM`，导出全局符号）
  - 生成 `.dynstr`（符号名称字符串表）
  - 生成 `.dynamic`（`DT_SYMTAB`/`DT_STRTAB`/`DT_STRSZ`/`DT_SYMENT`/`DT_SONAME`/`DT_INIT_ARRAY`/`DT_HASH`）
  - 生成 `.hash`（SysV 风格）
  - 生成 `.got`/`.got.plt`（`GLOB_DAT`/`JUMP_SLOT` 重定位条目）
  - 生成 `.plt`（x86_64 标准 16 字节 PLT 存根）
  - 生成 `.interp`（`/lib/ld-meuos.so.1`）
  - 发出 `PT_DYNAMIC`/`PT_INTERP`/`PT_GNU_RELRO`/`PT_GNU_STACK` 程序头
  - 实现 `R_X86_64_COPY`/`GLOB_DAT`/`JUMP_SLOT`/`RELATIVE` 重定位

**验收**: `mcc -shared -fPIC -o libfoo.so foo.c`（经 mt/ld 链接）

#### ld-pie: mt/ld `--pie`/`--no-pie` 支持

**文件**: `projects/meuos-toolchain/src/ld/`

**改动**: 与 `-shared` 共享 `ET_DYN` 输出路径，区别：
- PIE 有 `PT_INTERP` 且入口点为 `_start`
- `-no-pie` 为传统 `ET_EXEC`

**验收**: `mcc -pie -o app main.c`

#### mcc-pic-verify: mcc PIC 代码生成加固

**文件**: `projects/mcc/src/target/*/` + `driver/`

**现状**: x86_64 已通过 GOT/PLT 基本支持 PIC；其他架构未测试。

**改动**:
- x86_64: 验证 `-fPIC` 输出（数据→`@gotpcrel`，函数→`@plt`，TLS→`@gottpoff`/`@tlsgd`）
- aarch64: 验证 ADRP+LDR GOT 路径 + `@plt` 调用
- riscv64: 验证 `%got_pcrel_hi` + `%plt`
- 创建 `check-pic` 回归测试

**验收**: 全架构 `make check-pic` 通过

#### mcc-shared-mt: mcc driver `-shared` mt/ld 集成

**文件**: `projects/mcc/src/driver/host_toolchain.c`

**改动**: 移除 `if (!shared)` 条件中绕过 mt/ld 的逻辑。当 `MT_AS`/`MT_LD` 环境变量设置且 mt_target_supported 时，`-shared` 走 mt/ld。

**验收**: `MT_AS=... MT_LD=... mcc -shared -fPIC -o libfoo.so foo.c` 走 mt/ld 而非 host cc

#### ld-so: ld.so 动态链接器

**文件**: `projects/meuos-toolchain/src/rtld/`（新建）

**规模**: ~2500+ 行 C + 各架构自举汇编

**子任务**:
1. 入口自举（架构特定 `_start`，无栈/GOT 环境自举）
2. ELF 加载器（`mmap` PT_LOAD，页对齐）
3. DT_NEEDED 图遍历（递归加载依赖，循环检测）
4. 符号解析（`.dynsym`/`.hash` 搜索，加载顺序作用域）
5. 重定位应用（RELATIVE/GLOB_DAT/JUMP_SLOT/COPY/DTPMOD/DTPOFF/TPOFF）
6. 延迟绑定（PLT 初始存根 → `_dl_runtime_resolve` → GOT 填充）
7. TLS 初始化（PT_TLS → `tls_index` → `__tls_get_addr`）
8. RELRO 应用
9. init/fini 执行（`DT_INIT`/`DT_INIT_ARRAY`）
10. `LD_LIBRARY_PATH`/RPATH/RUNPATH 支持

**验收**: `LD_LIBRARY_PATH=. ./app` 正常执行

#### libc-dl: libc dlopen 接口

**文件**: `projects/meuos-libc/include/dlfcn.h`（新建）+ `src/dlopen.c` 等

**实现**:
- `dlopen()` → 调用 ld.so 加载新库
- `dlsym()` → 在已加载库或 `RTLD_DEFAULT`/`RTLD_NEXT` 中搜索
- `dlclose()` → 引用计数卸载
- `dlerror()` → 返回上次错误

**验收**: 运行 `dlopen("libfoo.so") + dlsym(handle, "func") + dlclose`

#### arch-p6-all: 全架构 p6-dynamic-link 适配

各架构需要的独特工作：
- mt/as: 添加 PLT/GOT/动态节区生成
- mt/ld: 各架构动态重定位类型（`R_AARCH64_GLOB_DAT`、`R_RISCV_RELATIVE` 等）
- ld.so: 各架构自举汇编、PLT 存根格式、函数调用约定
- mcc: 各架构 `-fPIC` 代码生成测试

---

## p8-dwarf — 子任务分解

### 现状

mcc 已经接受 `-g` 选项（`driver/main.c` 中 `case 'g': break;` — 记录但不输出）。
mt/ld 不处理 `.debug_*` 节区（静态链接直接透传）。
readelf 已实现部分 DWARF 节区头解析。

**核心设计**：DWARF 输出应在 mcc 的 **emit 阶段**（将 IR 指令转为汇编文本时）同时生成，而非在 IR 中建模 DWARF。emitfn 遍历 IR 时，按基本块和指令位置发出 `.loc` / `.cfi_*` 伪指令，由 mt/as 汇编成 DWARF 字节码，mt/ld 合并。

这样 DWARF 生成与 IR 优化解耦：优化 pass 改变 IR 时不需感知 DWARF。

### 14: mcc `-g` DWARF v5 生成

**文件**: `projects/mcc/src/emit/` + `projects/mcc/src/target/*/`

**子任务**:

1. **`-g` 门控**：`main.c` 中 `case 'g':` 改为设置 `debug_info = true` 而非空 `break`
2. **`.debug_line` 行号表**（最高优先级）：
   - emit 时跟踪当前源位置（file/line/col）
   - 发出 `.file N "name"` / `.loc F L C` 汇编伪指令
   - mt/as 汇编为 DWARF v5 `.debug_line` 行号程序
   - 验证：`readelf --debug-dump=line a.out` 显示正确的行号映射
3. **`.debug_info` + `.debug_abbrev`**（类型/变量信息）：
   - 遍历 AST 符号表，为每个函数/全局变量/类型生成 DIE（Debugging Information Entry）
   - 编译单元 DIE → 子程序 DIE → 局部变量 DIE → 类型 DIE
   - 发出 `.uleb128`/`.sleb128`/`.string` 汇编数据
   - 初始阶段：只输出函数名和行号范围
4. **`.debug_str`**（字符串表）：
   - 收集所有 DWARF 字符串，去重后写入
5. **`.debug_rnglists` / `.debug_loclists`**（地址范围）：
   - 函数和变量的地址范围列表

**实现策略**：
- 第一阶段：行号表（`.debug_line`）— 覆盖 80% 调试需求
- 第二阶段：函数签名和变量类型（`.debug_info` + `.abbrev`）
- 第三阶段：局部变量位置表达式（`.debug_loc` / `.debug_loclists`）

**参考**: cproc 的 DWARF 输出（`reference/cproc/`）、QBE 的 emit 框架

**验收**: `mcc -g -o hello hello.c && gdb ./hello -ex 'break main' -ex run -ex quit` 正常工作

### 15: mt/ld DWARF 节区合并

**文件**: `projects/meuos-toolchain/src/ld/link.c`

**改动**: 链接器需要：
- 透传 `.debug_*` 节区（已隐式处理——所有输入节区默认复制到输出）
- `.debug_line` 跨目标文件的 `.debug_str`/`.debug_abbrev` 去重
- `.debug_info` 中跨编译单元的引用修正

**验收**: `mcc -g -c -o a.o a.c && mcc -g -c -o b.o b.c && mcc -g -o ab a.o b.o && gdb ./ab` 正常工作

---

## 模板

| 准则 | x86_64 | aarch64 | riscv64 | i386 | loongarch64 | arm |
|------|--------|---------|---------|------|-------------|-----|
| mcc 后端 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 运行时 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/as | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/ld | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 全量构建 | ✅ | ✅ | ✅ bug-riscv64-emit | ✅ | ✅ bug-loong64-emit/bug-loong64-tls-reloc | ✅ |
| qemu 运行时验证 | ✅ 完整 | ✅ 完整 | ⚠️ exit=42 | ⚠️ qemu system | ⚠️ exit=42 | ✅ 完整 |
| TLS 端到端 | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ❌ bug-loong64-tls-reloc/bug-loong64-tls-errno | ✅ |
| self-rebuild | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ bug-loong64-emit | ❌ bug-arm-isel |
| 动态链接 | 🔄 ld.so (ld-shared 已完成) | 🔄 ld.so (ld-shared 已完成) | 🔄 ld.so (ld-shared 已完成) | 🔄 ld.so (ld-shared 已完成) | 🔄 ld.so (ld-shared 已完成) | 🔄 ld.so (ld-shared 已完成) |
| DWARF 调试信息 | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge |

---

## 详细说明

### bug-riscv64-emit — riscv64 emit assert(isreg(rb))

**文件**: `projects/mcc/src/target/riscv64/riscv64_emit.c:664`

**现象**: 编译 libc `string/error.o` 时断言失败：
```
mcc: src/target/riscv64/riscv64_emit.c:664: rv64_emitfn: Assertion `isreg(rb)' failed.
```

**原因**: emit 条件分支处理中 `rb` 可能是 slot 或 const。只有 `rtype(rb) == RSlot` 分支做了 `lw t6, %M0` 的 reg 降级，其他类型未处理。

**参考**: `riscv64_emit.c:658-664` — 与 bug-loong64-emit 同源。

**验收**: `make -C projects/meuos-libc ARCH=riscv64` 全量通过。

---

### bug-loong64-emit — loongarch64 emit assert(isreg(rb))

**文件**: `projects/mcc/src/target/loongarch64/loongarch64_emit.c:514`

**现象**: 编译 libc 时断言失败：
```
mcc: src/target/loongarch64/loongarch64_emit.c:514: la64_emitfn: Assertion `isreg(rb)' failed.
```

**原因**: 同 bug-riscv64-emit，条件分支 `rb` 未处理非 register 类型。

**参考**: `loongarch64_emit.c:510-516`。

**验收**: `make -C projects/meuos-libc ARCH=loongarch64` 全量通过。

---

### bug-arm-isel — arm isel slot %(null)

**文件**: `projects/mcc/src/target/arm/arm_isel.c`

**现象**: mcc 编译 arm backend 自身时 IR 警告：
```
mcc: slot %(null) is read but never stored to
```

**原因**: ARM isel 中某 IR slot 被引用但从未赋值。

**影响**: 阻塞 `check-sysroot-static`。

**验收**: `make -C projects/mcc check-sysroot-static` 全量通过。

---

### bug-i386-tls — i386 TLS 模型选择

**文件**: `projects/mcc/src/target/i386/i386_emit.c`（已实现两种重定位）+ isel/sema 层未正确选择模型

**现象**: 非静态 `_Thread_local` 在静态构建中链接失败，IE(`@gotntpoff`) 无法被静态链接器解析。

**原因**: mcc 的 isel/sema 层对 `_Thread_local` 变量做符号解析时，未根据 `--static`/`-fPIC` 选择正确的 TLS 模型。静态构建应选择 LE(`@ntpoff`)。

**关联**: AGENTS.md §10.2 列为「阻塞中」。见 `projects/mcc/src/target/i386/.todo`。

**验收**: `make check-i386` + `make check-i386-qemu` 中 TLS 测试通过。

---

### bug-loong64-tls-reloc — loongarch64 TLS LE 重定位损坏

**文件**: `projects/mt/ld` linker（loongarch64 支持模块）

**现象**: `.tdata` 段的 TLS 初始值被链接器破坏（`42` → 垃圾值）。

**原因**: mt/ld 的 loongarch64 TLS LE 重定位处理有 bug，`R_LARCH_TLS_LE_*` 类型应用不正确。

**已尝试修复**: 在 `apply.c` 中给 `R_LARCH_TLS_LE_HI20` 添加了 `+0x800` 舍入补偿，兼容交叉编译器可能使用 `addi.d` 而非 `ori` 的情况。

**仍有疑问**: `.tdata` 数据本身（初始值 42→49952）被破坏，而非仅是指令中的重定位值错误。
可能原因包括：
1. `.rela.tdata` 中存在错误的 `R_LARCH_64` 重定位覆盖了数据
2. QEMU loongarch64 的 TLS 模拟 bug
3. 静态 TLS 块布局计算错误（对齐填充导致偏移错误）

**需真机验证**：需要 loongarch64 交叉 GCC + qemu-loongarch64 环境做二进制级别的调试。
检查链接后的 ELF 中 `.tdata` 是否正确、`lu12i.w` 指令的立即数是否正确。

**关联**: 记录在 `projects/meuos-libc/src/arch/loongarch64/.todo`。

**验收**: 静态 TLS 变量的初始值正确保留。

---

### bug-loong64-tls-errno — loongarch64 TLS errno 回退

**文件**: `projects/meuos-libc/src/errno/`（或 TLS 相关）

**现象**: loongarch64 上 `errno` 使用 `static int` 而非 TLS 变量，多线程不安全。

**原因**: bug-loong64-tls-reloc 导致 TLS LE 不可用，libc 被迫回退到非线程安全的备用方案。

**验收**: loongarch64 qemu 下多线程 `errno` 隔离验证通过。

---

## 已解决的问题

| 旧 ID
|-------|------|------|
| riscv64-qemu-thread | riscv64 QEMU thread_cpu 全局变量（env qemu 10.1.0 已修复） | 🟢 已解决 |
| check-sysroot-static-path | check-sysroot-static temp 目录路径（已用 abspath 绕过） | 🟢 已绕过 |

---

## 模板

```markdown
### ID — 简短标题

**文件**: `path/to/file`

**现象**: 

**原因**: 

**验收**: 
```
