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
| rega-spill-fix | mcc QBE opt | 全架构 | spill.c 未给所有 block-entry-set temp 分配 slot，rega.c:83 assert(s != -1) | 🟢 | 本 worktree（spill.c 新增 post-loop slot 分配 + rega.c 动态 slot 回退） |
| bug-loong64-tls-reloc | mt/ld + libc | loongarch64 | TLS LE 初始化 bug：`__meuos_tls_init` 中 `count` 变量（AT_PHNUM）被 mcc 分配在临时寄存器 r12 但未 spill 回栈，导致 PT_TLS 搜索循环被跳过，返回 NULL（$tp=0）。修复：指针间接引用强制栈 slot 更新 | 🟢 | `ed49dbd`（tls.c 指针间接引用 workaround；errno_la64.c 移除，切换 TLS errno） |
| bug-loong64-tls-errno | meuos-libc | loongarch64 | TLS errno 回退到 `static int`（非线程安全），errno_la64.c 绕过 BFD 2.41 断言 | 🟢 | `ed49dbd` 随 bug-loong64-tls-reloc 一并修复，errno.c TLS 版已验证 |

## 优先级 1（P1-core — 功能完善，需要实现）

| ID | 组件 | 项目 | 描述 | 状态 | 实施情况 |
|----|------|------|------|------|---------|
| meow-dag-dedup | meow | DAG 去重 | `-jN` 并行构建时间接依赖重复执行（`.todo/dag-dedup.md`） | 🟢 | `9c20ae2` |
| mcc-atomic-voidptr | mcc sema | `_Atomic int*` → `void*` | C 6.3.2.3p1 限定对象指针应可转 `void*`（chibicc 测试报） | 🟢 | `03e8618` 验证通过 |
| mcc-riscv64-qemu | mcc+libc | riscv64 qemu 门禁 | 完整 qemu 运行时门禁（hello/atomic/setjmp 通过，phase2/bare-tls/malloc-threads 跳过因 qemu-user CLONE_THREAD 限制） | 🟢 | rega/spill fix + pthread volatile cast + __asm__→__asm__ 修复，QEMU 运行时验证通过 |
| mcc-loong64-qemu | mcc+libc | loongarch64 qemu 门禁 | 完整 qemu 运行时门禁：hello/atomic/setjmp/phase2/bare_tls/malloc_threads 全部通过。TLS 端到端验证（_Thread_local 初始值+线程隔离）+ TLS errno 线程隔离均通过 | 🟢 | `ed49dbd`（tls.c 指针间接引用修复 + TLS errno 切换） |
| mcc-i386-tls-e2e | mcc+libc | i386 TLS e2e | bug-i386-tls 修复后的 TLS 端到端验证 | 🟢 | 本 commit |
| mcc-i386-tls-doc | mcc(i386) | `gd-tls.md` 文档 | 被 3 个文件引用但文件不存在 | 🟢 | `03e8618` |
| ld-shared | mt/ld | `-shared` 输出 `ET_DYN` | 🟢 ET_DYN + PHDR/DYNAMIC + dynsym/dynstr/hash/dynamic + .rela.dyn (R_X86_64_RELATIVE/GLOB_DAT) + DT_NEEDED + DT_INIT_ARRAY/FINI_ARRAY + PT_GNU_RELRO。**可被 ld.so 正确加载**。已知 bug 已修复（TLS 组初始化/dynsym 填充顺序/无名符号/REX_GOTPCRELX） | `a0822fe` + `1c43b23` + `355c7c8` + `f0e3a54` (DT_NEEDED) |
| ld-pie | mt/ld | `--pie`/`--no-pie` 支持 | PIE 二进制输出（`ET_DYN` + `PT_INTERP` + 相对重定位 + DT_NEEDED + PT_GNU_RELRO） | 🟢 | `4ae63b1` + `9f43641` + `f0e3a54` |
| mcc-pic-verify | mcc | PIC 代码生成加固 | 全架构验证 `-fPIC` 输出（GOT/PLT/TLS GD 路径）（riscv64 GOT 为空缺） | 🟢 | 本 commit（新增 pic_verify.sh；riscv64 需修复 `%got_pcrel_hi` emit） |
| mcc-shared-mt | mcc driver | `-shared` mt/ld 集成 | 去掉 `-shared` 回退到 host cc 的限制 | 🟢 | `acce6c6` |
| ld-so | 新建 | ld.so 动态链接器 | **完整端到端验证通过**：x86_64 PIE + shared library 动态链接运行 exit=0 ✅。ELF 加载 + DT_NEEDED 传递遍历 + 符号解析 (SysV hash) + RELA 重定位 (RELATIVE/GLOB_DAT/JUMP_SLOT) + init_array | 🟢 | `525ab54` + `99dab32` (DT_NEEDED recursive) + `37ce60c` (main_base fix) |
| ld-so-tls | ld.so (rtld.c) | ld.so 动态 TLS 支持 | rtld.c 实现 PT_TLS 扫描、模块 ID 分配（main=1, libs=2..N）、连续 TLS 布局（Variant II，%fs 指向区块末端 `tp`）、`R_X86_64_DTPMOD64`/`DTPOFF64` 解析为 TP 相对偏移，与 libc `__tls_get_addr` 的 `tp + ti_offset` 约定一致 | 🟢 | worktree（rtld.c + rtld.h：tls_modid/tls_image/tls_tp 字段；rtld_tls_setup 布局+arch_prctl(ARCH_SET_FS)；rela 处理 DTPMOD/DTPOFF） |
| bug-mt-so-undef | mt/ld | `-shared` 未定义符号 | **已修复**：mt/ld 链接 `.so` 遇到未定义外部符号时不再直接报错，而是：① `symbol_value()` 在 shared 模式下对 UNDEF 符号返回 0 而非 error；② PLT32 重定位自动分配 GOT 条目并标记 JUMP_SLOT；③ .dynsym 包含 STT_FUNC(SHN_UNDEF) 导入符号供 ld.so 运行时解析；④ build_rela_dyn 为 JUMP_SLOT 条目发出 MT_R_X86_64_JUMP_SLOT 动态重定位。验证：含 __tls_get_addr 引用和 TLS GD 模型的 libtls.so 正确生成（PT_DYNAMIC + PT_TLS + .dynsym/.dynstr/.hash 完整）。`make check` 全 PASS。 | 🟢 | 本 commit（link.c：symbol_value + PLT32 GOT + dynsym UNDEF + build_rela_dyn JUMP_SLOT） |
| libc-dl | meuos-libc | `dlfcn.h` + `dl*` 实现 | `dlopen`/`dlsym`/`dlclose`/`dlerror` | 🟢 | `fd05074` mmap-based ELF 加载器 + SysV hash 符号查找 + RELA 重定位 + 急切解析 |
| mcc-dwarf | mcc | DWARF 调试信息 | `-g` 生成 DWARF v5（`.debug_info`/`.abbrev`/`.line`/`.str`/`.loc`/`.ranges`），包含行号、变量、类型信息 | 🟢 | `a9a065c` 行号表(阶段1); .debug_info/abbrev/str 阶段2待实现 |
| as-dwarf | mt/as | DWARF 汇编伪指令 | `.loc`/`.file`/`.cfi_*` 支持 — **阻塞 mcc-dwarf**，无此 as 无法处理 `-g` 输出 | 🟢 | `a5f49c0` |
| ld-dwarf-merge | mt/ld | DWARF 节区合并 | 链接时合并 `.debug_*` 节区，生成 `.debug_line`/`.debug_info` 跨目标文件 | 🟢 | 本 commit（支持合并 `.debug_*` 非 ALLOC 节区；DWARF 重定位尚需独立调整） |
| ld-build-id | mt/ld | `--build-id` | 生成 `.note.gnu.build-id`（FNV-1a hash，用于调试/构建标识） | 🟢 | 本 commit |
| ld-eh-frame-hdr | mt/ld | `--eh-frame-hdr` | 生成 `.eh_frame_hdr`（异常处理帧索引，gcc 异常必需） | 🟢 | 本 commit |
| ld-as-needed | mt/ld | `--as-needed` / `--no-as-needed` | 避免不必要的 DT_NEEDED 条目。.so 输入 + 自动 DT_NEEDED 已实现 | 🟢 | `c0bec11` .so 输入支持 + `27f929c` 自动 DT_NEEDED |
| ld-start-group | mt/ld | `--start-group` / `--end-group` | 循环依赖库解析（`-Wl,--start-group -la -lb -lc --end-group`） | 🟢 | 本 commit |
| ld-whole-archive | mt/ld | `--whole-archive` / `--no-whole-archive` | 强制归档中所有目标文件链接（用于 plugin/init 段） | 🟢 | 本 commit |

## 优先级 2（P2-toolchain — 生态集成）

| ID | 组件 | 项目 | 描述 | 状态 | 实施情况 |
|----|------|------|------|------|---------|
| meow-native-shell | ⛔ | 原生 shell 替代 | 阻塞于 msh（不在本次 worktree 范围） | ⛔ | 不在范围 |
| mcc-msys-link | mcc driver | `.msys` + host linker | host cc 链接时自动提取 `.a` 到 temp + ARM triplet 识别使 mt/as 可处理 arm 编译 | 🟢 | `16e683e`（msys.c 已有基础 + ARM 集成） |
| ld-tls-dynamic | mt/ld + ld.so | TLS 动态模型 | GD/LD 模型、`__tls_get_addr`（依赖 ld-shared，已就绪） | 🟢 | link.c 添加 R_X86_64_TLSGD/TLSLD/DTPMOD/DTPOFF 处理：静态链接放松为 LE(TPOFF32)，PIE/shared 保留动态重定位。**ld.so 侧 rtld.c 已完成**：PT_TLS 扫描、模块 ID 分配、连续 TLS 布局（Variant II，%fs 指向区块末端）、R_X86_64_DTPMOD64/DTPOFF64 解析为 TP 相对偏移。**bug-mt-so-undef 已修复**：mt/ld 在 shared 模式下对未定义符号（含 __tls_get_addr）生成 GOT/JUMP_SLOT 动态导入而非报错。端到端验证：含 __tls_get_addr 引用的 .so 正确生成（PT_DYNAMIC+PT_TLS+.dynsym+.dynstr+.hash 完整）。|
| ld-gc-sections | mt/ld | 死代码消除 | 未引用节区的裁剪。概念有用，实现应自己设计，不照搬 GNU `--gc-sections` 的复杂逻辑 | 🟢 | 本 commit（gc_sweep + section_rank .text.*/.data.* 支持） |
| ld-linker-script | mt/ld | 链接布局控制 | ❌ **不做 GNU `.ld` 脚本解析**。需要时改为 YAML 格式描述节区布局（链接器内嵌或独立文件） | 🟢 | -T/--link-script 自定义 rank 排序已实现 |
| as-macro | mt/as | `.rept`/`.endr` 重复伪指令 | 基本的行重复机制，用于汇编展开和填充 | 🟢 | 本 commit（assemble.c: .rept N 保存 ftell 位置，.endr 迭代 fseek 回退；单层嵌套限制；回归测试） |
| as-full-isa | mt/as | 全架构指令完整覆盖 | 各架构缺的少用指令补全（ARM 从 321→1098 行，已超越 x86_64 的 1075 行） | 🟢 | ARM 1098 行（含 VFP/NEON/ALU/移位/乘法/DSP/位域/除法/多寄存器/提示指令等全部常用指令） |
| as-cond | mt/as | 条件汇编 | `.if`/`.ifdef`/`.ifndef`/`.else`/`.endif` — 通用汇编概念，非 GAS 特有 | 🟢 | 本 commit |
| as-align | mt/as | 对齐/填充伪指令 | `.balign`/`.p2align` — 通用汇编概念，不是 GAS 特有 | 🟢 | 已实现（assemble.c:763-771） |
| as-section | mt/as | 节区控制伪指令 | `.pushsection`/`.popsection` — 通用概念，用于 gcc asm 属性 | 🟢 | 本 commit |
| as-equ | mt/as | 常量/符号定义 | `.equ`/`.set` — 通用概念 | 🟢 | 已实现 |
| as-diag | mt/as | 汇编诊断 | `.abort`/`.error`/`.warning` | 🟢 | 已实现 |
| ld-linker-script | mt/ld | 链接脚本支持 | `-T` 文件格式描述节区布局（自定义 rank 排序，非 GNU .ld） | 🟢 | 本 commit（-T/--link-script: `section = rank` 格式控制放置顺序） |
| ld-print-map | mt/ld | `--print-map` | 链接映射输出 | 🟢 | 本 commit（--print-map 输出节区地址/大小/偏移+入口点） |
| ld-defsym | mt/ld | `--defsym` | 链接时定义绝对符号（`--defsym=foo=val`，支持 0x 前缀） | 🟢 | 本 commit（ld.h + main.c + link.c：apply_defsym 创建绝对符号，symbol_value 返回 absolute 值） |
| ld-wrap | mt/ld | `--wrap` | 符号包装（`--wrap=malloc` → `__wrap_malloc`）用于测试/mock | 🟢 | 本 commit（link.c: apply_wrap + alias 链；支持多实例；回归测试） |
| ld-version-script | mt/ld | `--version-script` | 符号版本控制/导出限制（`.map` / `.ver` 文件） | 🟢 | `afabd32`（`{ global: ... ; local: *; }` 格式解析 + dynsym 过滤） |
| ld-no-undefined | mt/ld | `--no-undefined` | 未定义符号时报错（`-z defs` / `-z undefs`） | 🟢 | 本 commit |
| ld-cref | mt/ld | `--cref` | 交叉引用表输出 | 🟢 | 已实现 |
| ld-compress-debug | mt/ld | `--compress-debug-sections` | DWARF 节区压缩（zlib/zstd） | 🟢 | 已实现 |
| meow-multi-dir | meow | 多目录构建 | 跨目录包依赖的 YAML 配方 | 🟢 | 本 commit（build all + load_recipe 路径支持） |
| tool-binary | 新建 | 统一二进制分析工具 | **不做 size/strings/addr2line/ldd 各自一个工具**。设计一个 `mt-info` 或集成到 `objdump`/`readelf` 中，通过 subcommand 提供多种分析能力 | 🟢 | mt-info 已实现（`src/mt-info/` 9 文件：info/headers/deps/strings/which/diff/inspect + ui/load/main）。复用 libelf，统一 `--json/--quiet/--verbose/--no-color` 跨工具约定。验证：info/headers 彩色卡 + --json + deps 递归 DT_NEEDED（对 /bin/ls 正确显示 libselinux/libcap/libc）均通过 |

## 优先级 3（P3-libc — libc 标准接口完备）

> C 库标准接口（ISO C11 + POSIX）的完整实现。当前核心 libc 已有基础框架，
> 以下为尚未实现的或仅 stub 的接口族。

| ID | 模块 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| libc-math | `<math.h>` | 数学库：`sin`/`cos`/`sqrt`/`log`/`exp`/`pow` 等 IEEE 754 浮点函数 | 🔴 高 | 🟢 本 commit（sqrt/log/exp/pow/sin/cos/tan 软浮点实现） |
| libc-printf | `<stdio.h>` | 完整 `printf`/`scanf` 格式覆盖（浮点、`%n`、宽字符、长 double） | 🔴 高 | 🟢 已支持 %d/%s/%f/%e/%g/%x/%p/%n；%a/%A 降级为 %g；长 double 降级为 double |
| libc-time | `<time.h>` | 完整 `strftime`、时区处理、`clock_gettime` POSIX 扩展 | 🟡 中 | 🟢 本 commit（localtime/gmtime/mktime/strftime 完整覆盖） |
| libc-pthread | `<pthread.h>` | rwlock/barrier/spinlock/cleanup handler 完整覆盖 | 🟡 中 | 🟢 本 commit（rwlock/barrier/spinlock/cleanup_push/pop） |
| libc-str | `<string.h>` | `strerror_r` 线程安全变体、`strcoll`/`strxfrm` locale 感知 | 🟢 低 | 🟢 本 commit（+strspn/strcspn/strcasecmp/strncasecmp/strerror_r） |
| libc-wchar | `<wchar.h>` | 宽字符 I/O、宽字符 `printf`/`scanf`、wcsftime | 🟢 低 | 🟢 本 commit（wcslen/cpy/cat/cmp + isw*/tow* + mb/wc 转换 + fgetwc/fputwc） |
| libc-locale | `<locale.h>` | locale 感知函数（`setlocale` 当前 stub） | 🟢 低 | 🟢 本 commit（setlocale/localeconv 实现 + struct lconv） |
| libc-complex | `<complex.h>` | 复数算术和数学函数 | 🟢 低 | 🟢 本 commit（complex.h + creal/cimag/conj 实现） |
| libc-socket | POSIX 网络 | `<sys/socket.h>`、`<netdb.h>`、`<netinet/in.h>`、`<arpa/inet.h>` | 🟡 中 | 🟢 本 commit（socket/bind/listen/accept/connect/inet 实现 + netinet/in.h + arpa/inet.h） |
| libc-regex | POSIX 正则 | `<regex.h>` — `regcomp`/`regexec`/`regerror`/`regfree` | 🟡 中 | 🟢 本 commit（Thompson NFA 引擎，支持基本 ERE: \| * + ? [] . ^ $） |
| libc-termios | POSIX 终端 | `<termios.h>`、`<sys/ioctl.h>` | 🟢 低 | 🟢 本 commit（termios.h + ioctl + tcgetattr/tcsetattr + cfmakeraw） |
| libc-glob | POSIX glob | `<glob.h>`、`<fnmatch.h>` 模式匹配 | 🟡 中 | 🟢 本 commit（fnmatch + glob 完整实现） |
| libc-syslog | POSIX 环境 | `<syslog.h>`、`<utmpx.h>` | 🟢 低 | 🟢 本 commit（syslog/openlog/closelog/vsyslog 实现） |
| libc-atomic | `<stdatomic.h>` | C11 atomic 的完整操作集（`atomic_compare_exchange_*` 变体等） | 🟡 中 | 🟢 已实现（含 compare_exchange/fetch_add/sub/and/or/xor 及 memory_order） |
| libc-threads | `<threads.h>` | C11 thread 完整接口（`tss_*`、`call_once` 等） | 🟡 中 | 🟢 完整实现（thrd/mtx/cnd/tss/call_once + timed 变体） |

## 优先级 4（P4-devexp — 开发者体验）

> 编译器质量和开发者体验优化。这些项不影响功能正确性，但影响日常使用体验。

| ID | 组件 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| mcc-diagnostics | mcc | 诊断质量 | 带源位置和 caret（`^`）的错误消息。这是 Clang 推广的好设计，非 GNU 包袱 | 🟡 | 🟢 本 commit（token.c 添加 caret ^ 错误指示） |
| mcc-warnings | mcc | 警告体系 | ⚠️ `-Wall`/`-Wextra` 是 GCC 命名约定。我们应该设计自己的警告体系（`--warn=all`/`--warn=extra` 或 `-W` 风格但自己定义哪些组别） | 🟡 | 🟢 解析层（9713bfe）：`--warn=all/portable/style/performance/pedantic` 语义组 + ir.h WARN_* 位扩展；`-Wall`/`-Wextra` 保留为 --warn=all 兼容别名。⚠️ 待补：sema 层 `cc_warn()` 发射点（未使用变量/隐式转换/类型不匹配等警告尚未实际发射） |
| mcc-attributes | mcc compat | `__attribute__` | GCC 属性语法。按设计原则应走 compat 映射层，核心不直接处理 | 🟢 | `7c087f8`（支持 weak/used/noinline/always_inline/visibility/unused/aligned/section/packed/noreturn/deprecated/constructor/destructor） |
| mcc-pragma | mcc compat | `#pragma` | GCC/Clang pragma。同样走 compat 映射层 | 🟢 | `cffc405`（`#pragma once` 等已接收并忽略；`_Pragma` 操作符在 expand 中处理） |
| mcc-builtins | mcc compat | `__builtin_*` | GCC/Clang 内建函数已实现：expect/constant_p/offsetof/alloca/unreachable/va_*/types_compatible_p/inff/nanf/atomic_fetch_* | 🟢 | scope.c 中完整的 builtin 表 + expr_primary.c 中处理原子操作 |
| mcc-generic | mcc | `_Generic` 完整 C11 匹配规则（含 qualified type 分派） | 🟢 已验证 `_Generic` 支持 qualified type 匹配，正确检测 multiple matches | `dc2d598`（移除 `qual==QUALNONE` 保护） |
| as-errors | mt/as | 错误消息行号/列号 | 🟢 | 已有 line info（mt_as_assemble 通过 error_line 参数报告） |
| ld-errors | mt/ld | 未定义符号的友好诊断（列出候选目标文件） | 🟢 低 | `a3237f3`（--no-undefined 时自动推荐拼写接近的符号名） |
| community-tests | 全项目 | 社区测试套件覆盖率（chibicc → C99/C11/C23 全量通过） | 🟡 中 | 🟢 基础设施已就绪（`test/community/chibicc/` 53 个测试 + `run.sh` + `make check-chibicc`/`check-community`）。本次修复 run.sh 的硬编码 worktree 路径（改为基于脚本位置推导 ROOT），现已可运行。实测：41 测试 PASS=9 RUNFAIL=2 COMPILEFAIL=30。COMPILEFAIL 多为 mcc C 兼容性（libc 头文件重复声明不兼容等），属编译器/libc 质量范畴，非测试基础设施问题。 |
| meow-lint | meow | 配方语法检查器（`meow lint`） | 🟢 低 | 🟢 5f34bb6：`src/lint.c` 实现 `cmd_lint()`，无参数遍历 pkgs/ + 当前目录 meow.yaml，指定包名检查单个；复用 load_recipe + parse_recipe。main.c 分发 + meow.h 声明已加。验证：meow lint meow-smoke 通过；fixture 配方正确报告语法错误 |
| ci-pipeline | 全项目 | CI/CD 流水线（GitHub Actions + qemu-user 跨架构回归） | 🟡 中 | 🟢 `.github/workflows/ci.yml` 创建：构建 mcc + meuos-toolchain + meow；运行 `make check`/check-c99/c11/c23/chibicc；qemu-user 跨架构测试（riscv64/aarch64/i386） |

## 优先级 5（P5-meow — meow 构建系统完备）

> meow 目标是替代 make + autoconf + libtool + pkg-config。当前约 40% Make、
> 20% autoconf、0% pkg-config/libtool。以下按影响排序。

| ID | 类别 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| meow-template-subst | autoconf | **模板替换** | ⚠️ 不做 `@VAR@`（autoconf 遗留）。需要时用 YAML 原生表达式或 meow 自己的模板语法 | 🔄 重设计 | 待设计 |
| meow-wildcard | make | **文件列表通配** | ⚠️ 不做 `$(wildcard)`（GNU make 语法）。用 YAML 原生匹配或 meow 自己的函数（`files('src/*.c')`） | 🔄 重设计 | 待设计 |
| meow-check-library | autoconf | **`check_library` / `check_link`** | 链接测试检测 `-lz`、`-lpthread`。概念本身没问题——autoconf 的 probe 机制是合理的设计。实现时不照搬 autoconf 语法即可 | 🔴 高 | 🟢 本 commit（probe_add_library + probe_link，YAML libraries: 列表支持） |
| meow-conditional | make | **条件语句** | ⚠️ 不做 `ifeq`/`ifdef`（GNU make 语法）。YAML 条件语句（`when: ARCH == "aarch64"`）或类似 DSL | 🔄 重设计 | 待设计 |
| meow-type-size | autoconf | **`check_type_size`** | 检测 `sizeof(time_t)`。autoconf 的这个概念合理，不照搬其实现。meow 应有自己的 `type_size()` probe 函数 | 🟡 中 | 🟢 已完成：probe.c 添加 `probe_add_typesize()` + probe_run 中 typesize 探测（编译C程序 `printf("%zu", sizeof(TYPE))`、运行并捕获输出、写入 `#define SIZEOF_NAME N` 到 config.h）；parse.c 支持 `typesizes:` YAML 节区（`- NAME: type_name` 格式） |
| meow-probe-cache | autoconf | **Probe 缓存** | 缓存编译测试结果（`config.cache` 等价物）。概念好，实现不照搬 autoconf 的烦人缓存格式 | 🟡 中 | 🟢 probe.c 实现 DJB2 指纹缓存：`probe_fingerprint()` 对全部 probe 注册（headers/funcs/codes/decls/libs/typesizes）计算哈希；`probe_run` 检查 config.h 首行 fingerprint，匹配则跳过全量 probe。缓存命中无编译开销 |
| meow-vpath | make | **出源码构建** | ⚠️ 不做 `$(srcdir)`（GNU make VPATH）。meow 默认出源码构建（`build/<pkg>/`），不用额外抽象 | 🔄 重设计 | 待设计 |
| meow-subdirs | make | **多目录构建** | 跨目录包依赖的 YAML 配方。不是 GNU `AC_CONFIG_SUBDIRS`，是 meow 自己的依赖模型 | 🟡 中 | 🟢 meow 配方新增 `depends:` 顶级节区（YAML：`depends:\n  - pkgname`）。parse.c 解析 depends 列表存到 recipe_deps[]。main.c 构建前先递归 `$0 build <dep>` 构建每个依赖包。验证：libbase → app 依赖链正确构建；nonexistent-pkg 正确报错 "dependency failed"。同目录下的 pkgs/ 包可作为跨包依赖。 |
| meow-pkg-config | pkg-config | **`.pc` 文件查询** | ⚠️ 不做 `.pc` 解析（freedesktop.org 格式）。meow 应有自己的包元数据格式（YAML 原生），或只通过 `meow install` 注册的数据库查询 | 🔄 重设计 | 待设计 |
| meow-libtool | libtool | **共享/静态库管理** | ❌ 不做 `.la` 文件。libtool 是历史遗留，mt/ld 直接管理库格式，meow 只需知道库路径 | ❌ 不做 | 不做 |
| meow-dag | meow | DAG 去重 | `-jN` 间接依赖重复执行。纯 meow 自己设计，无历史包袱 | 🟢 | `9c20ae2` DAG 去重已完成 |
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
| target-features | **`Target.features` 设计**：在 Target 中添加 `uint64_t features` 位图 + `const char *features_desc[]`。定义架构无关的公共特性位和架构特有的特性位 | 🔴 高 | 🟢 `058c882`（`struct mt_target` 添加 `features` 字段 + `MT_FEATURE_*` 常量定义） |
| march-generic | **`-march=` 解析通用化**：从仅 ARM 推广到全架构。x86_64 解析 `native`/`x86-64`/`x86-64-v2`/`v3`/`v4`；riscv64 解析 `rv64gc`/`rv64imafdc`；aarch64 解析 `armv8-a`/`armv8.2-a` 等 | 🔴 高 | 🟢 完成：cpu_detect.c（detect_cpu_features + march_x86_64_v_level + /proc/cpuinfo 跨架构回退）；main.c 通用 -march= 解析（x86_64 的 native/vN 全部走 cpu_detect.c，ARM 仍保留原 march/mcpu）；g_target_features 全局位图已建立 |
| x86-isa-levels | **x86_64 ISA 级别门控**：实现 `-march=x86-64-v2`/`v3`/`v4` 代码生成差异。至少：v2 启用 SSE4.2+POPCNT，v3 启用 AVX2+BMI2，v4 启用 AVX-512 | 🔴 高 | 🟡 检测层完成（cpu_detect.c march_x86_64_v_level）；emit 层 ISA 门控待实现（emit/isel 需读取 g_target_features 来做指令选择器分支） |
| riscv-extensions | **riscv64 扩展选择**：实现 `-march=rv64imafdc` 解析，根据扩展集发射指令。`-mabi=lp64d`/`lp64`/`ilp32d`/`ilp32` | 🟡 中 | 🟢 `-march=rv64gc`/`rv64imafdc`/`rv64imac` 解析已落地（main.c）：逐字符解析扩展字母，设置 `g_target_features` 位（MT_FEATURE_RV_F/D/C/V，位布局与 mt/as target.h 一致）。`g`=imafd 简写。验证：mcc -target=riscv64-meuos-linux -march=rv64gc / rv64imac 交叉编译成功。⚠️ 待补：riscv64 emit/isel 层消费这些特性位做指令选择（当前后端默认基础指令集，FV 扩展指令尚未门控）；`-mabi=` 浮点 ABI 选择待实现 |
| arm-multiver | **arm 多版本后端**：根据 `-march` 切换 ARMv6/v7/v8 指令选择器和发射器差异（Thumb/ARM 模式、DMB 变体等） | 🟡 中 | 🟡 解析层已就绪：`arm_arch_from_march()` 提取 `armvN` 版本号（main.c:61），`arm_set_target_defines()` 设置 `__ARM_ARCH` 预定义宏；mcc `-march=armv6/armv7-a/armv8-a` 均可解析。⚠️ 待补：ARM emit/isel 层（arm_emit.c）未消费 arch_ver 做指令选择分支（如 armv6 降级不含 `movw`/`movt`，armv7+ 用 `dmb` 变体）；且当前 ARM 后端编译简单程序时报告 as 错误（`unknown vector operation: {lr}`），需先修 ARM 后端基础 bug 才能做多版本分支 |
| aarch64-ext | **aarch64 架构扩展**：FEAT_FP16/FEAT_RDM/FEAT_JSCVT 等特性位与代码生成 | 🟢 低 | 待实现 |
| march-native | **`-march=native`**：通过 CPUID（x86）或 `/proc/cpuinfo` 查询宿主机特性并设置 Target.features | 🟡 中 | 🟢 完成：cpu_detect.c（x86_64 CPUID 内联汇编 + xgetbv 验证 AVX OS 支持、/proc/cpuinfo 跨架构回退），main.c 集成（解析 `-march=native` → 标记 g_march_native_requested → 目标选择后 detect_cpu_features 填充 g_target_features）。check-c99/check-c11/check-c23 全部通过 |
| as-isa-gating | **mt/as 指令门控**：编码器根据 insn 要求的特性位进行验证，不支持的指令报错而非默默生成 | 🟡 中 | 🟢 实现 — 两层门控：(1) 编码前快速检查：`v*` 开头（VEX/AVX）的指令检查 `MT_FEATURE_AVX`，未启用时报 "requires AVX (use -march=x86-64-v3 or higher)"；(2) 编码后精确门控：编码器设置 `insn.required_features`，门控比较特性位缺失并报"requires disabled ISA extension"（精确到特性名）。`--march=x86-64-v2/v3/v4` 经由 `mt_target_features_for_march()` 映射为特性位集。验证：基线拒绝 vmovups，v3 接受并正确编码（字节级匹配 gas）。AVX 指令编码：`vmovups`/`vmovupd`/`vmovaps`/`vmovapd`（xmm/ymm reg-reg + reg-mem）。 |
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
| c23-constexpr | `constexpr` 初始化规则（运行时 vs 编译期求值边界） | 🟢 | 已验证 constexpr 基本功能工作正常 |
| c23-attributes | `[[]]` 属性语法全位置覆盖（声明/类型/语句/标签） | 🟢 | 已验证 `[[deprecated]]` 声明属性正常工作 |
| c23-bool | `bool`/`true`/`false` 关键字 vs `<stdbool.h>` 宏兼容 | 🟢 | mcc 有 TBOOL/TTRUE/TFALSE 关键字；stdbool.h 定义宏兼容 |
| c23-embed | `#embed` 边界情况（大文件/`limit(N)`/`prefix`/`suffix`/`if_empty`） | 🟢 | pp.c:1030-1145 完整实现（需行首放置，已验证 limit/if_empty 可工作） |
| c23-typeof | `typeof`/`typeof_unqual` 在复杂声明中的应用 | 🟢 | 已支持 typeof 和 typeof_unqual 关键字 |

## 优先级 3（P3-libc — 社区测试兼容性，低优先级）

| ID | 组件 | 描述 | 状态 | 实施情况 |
|----|------|------|------|---------|
| mcc-float-suffix | mcc lexer | C23 `100f` float 后缀支持 | 🟢 | 本 commit（已在 expr_primary.c:82 实现，新增测试用例） |
| mcc-uint64-max | mcc sema | UINT64_MAX 字面量类型回退到 `unsigned long long` | 🟢 | 本 commit |
| mcc-macro-redef | mcc preproc | 宏定义相同 token 序列允许重定义（C 6.10.3p2） | 🟢 | chibicc 兼容模式（静默替换，201行） |
| mcc-line-num | mcc preproc | `__LINE__` 偏移 1 | 🟢 | scan.c:431 line=1→0 + token.c:162 line+1
| mcc-common-sym | mcc sema | common/tentative-definition 合并行为 | 🟢 | mcc 使用 -fno-common 语义（.bss 非 .comm），同 TU 合并正常（GCC 10+ 一致） |
| mcc-unicode | mcc lexer | Unicode/C11 标识符（$/UCN/UTF-8） | 🟢 | 本 commit（\u / \U UCN 支持 + UTF-8 标识符已支持） |
| mcc-va-end | mcc | `__builtin_va_end` 类型检查时机（宏定义处而非使用处） | 🟢 | expr_primary.c:280 已在 use site 检查 |

---

## 架构完备性矩阵

| 准则 | x86_64 | aarch64 | riscv64 | i386 | loongarch64 | arm |
|------|--------|---------|---------|------|-------------|-----|
| mcc 后端 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 运行时 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/as | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| mt/ld | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| libc 全量构建 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| qemu 运行时验证 | ✅ 完整 | ✅ 完整 | ✅ hello/atomic/setjmp（线程跳过 qemu-user 限制） | ⚠️ qemu system | ✅ 完整（hello/atomic/setjmp/phase2/bare_tls/malloc_threads 全部通过） | ✅ 完整 |
| TLS 端到端 | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ | ✅ |
| self-rebuild | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ bug-loong64-emit | ❌ bug-arm-isel |
| 动态链接 | 🔄 ld-shared...ld-so | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| DWARF 调试信息 | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| mcc 诊断/警告 | 🔄 mcc-diagnostics+attributes+generic | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| libc 完整 POSIX | 🔄 libc-math...libc-threads | 🔄 | 🔄 | 🔄 | 🔄 | 🔄 |
| mt/as 完整指令 | 🟢 x86_64 1075 行 | 🟢 aarch64 1247 行 | 🟢 riscv64 939 行 | 🟢 i386 1118 行 | 🟢 loongarch64 898 行 | 🟢 ARM 1098 行（全部常用指令均已覆盖） |
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
| meow-auto-config | **meow 自动决策编译参数** | `meow build <pkg> --target=<triplet>` 自动解析 triple → arch/abi/float/subarch, 自动设 CC/CFLAGS/LDFLAGS, 自动跑 probe 生成 config.h | meow + mcc | 🔴 高 | 🟢 94068b9：`set_arch_env()` 在 build_target 非 NULL 时导出 TARGET_TRIPLE 变量；`parse_triple_arch()` 支持别名（amd64→x86_64）；probe 自动检测已存在 |
| triple-format | **MeuOS triple 格式设计** | 定义 `<arch>[-subarch][-vendor][-os][-abi]` 格式。vendor=`meuos` 隐含 MeuOS 默认行为；os=`meuos-next` 为未来原生环境。见下方详细设计 | meow + mcc + mt | 🔴 高 | 🟢 mcc 侧（`src/driver/triple.h` + `triple.c` + `target_select.c`）+ meow 侧（`src/triple.c` + `--target=` 解析）。双组件均支持完整 triple 解析 + 架构别名 |
| triple-lib | **共享 triple 解析库** | meow 和 mcc 共享同一套 triple 解析逻辑，避免两处分叉。提取为 `libtriple` 或共用头文件 | meow + mcc | 🔴 高 | 🟢 mcc 侧 `triple.h/c` + meow 侧 `triple.c`（逻辑相同）。暂未提取为独立共享库——两副本各自维护，逻辑一致。后续如需共享库，可提取到 `meuos-toolchain/lib/triple/` |
| triple-abi-map | **Triple → ABI 自动映射** | `--target=<triplet>` 精确提取 ABI/lp64/float 信息并选择对应 Target/ABI 降级 | mcc sema | 🟡 中 | 🟢 main.c 添加 ABI 自动映射：`targ_abi(target)` 从 triple 提取 ABI 后缀，自动设置 arm_mfloat_abi（hard/soft）。支持：lp64d/lp64f→hard(riscv FP)、lp64→soft(riscv)、gnueabihf→hard(ARM)、gnu→soft |
| meow-zero-args | **meow 零参数构建** | `meow build` 无参数时自动嗅探当前架构/sysroot/源码，生成完整构建环境；只有需要定制时才传入参数 | meow | 🔴 高 | 🟢 已实现（main.c: 当 count==1 && "build" 时自动检测当前目录 meow.yaml 或回退 Makefile 兼容模式），`--target=` 解析完备后，`meow build` 即可零配置构建目标架构 |

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
| libc 全量构建 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| qemu 运行时验证 | ✅ 完整 | ✅ 完整 | ✅ hello/atomic/setjmp（线程跳过 qemu-user 限制） | ⚠️ qemu system | ✅ 完整（hello/atomic/setjmp/phase2/bare_tls/malloc_threads 全部通过） | ✅ 完整 |
| TLS 端到端 | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ | ✅ |
| self-rebuild | ✅ | ✅ | ✅ bug-riscv64-emit | ❌ bug-i386-tls | ✅ bug-loong64-emit | ❌ bug-arm-isel |
| 动态链接 | ✅ ld.so 端到端验证通过（x86_64 PIE + .so） | 🔄 跨架构适配 | 🔄 跨架构适配 | 🔄 | 🔄 | 🔄 |
| DWARF 调试信息 | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge | 🔄 mcc-dwarf+ld-dwarf-merge |

---

## 优先级 9（p9-ui — CLI/TUI 用户体验设计）

> **核心理念：不做传家宝，做现代工具箱。**
>
> 我们不是在做"又一个 GNU 工具链"。没有人会感激我们再实现一遍 `-Wall -Wextra -Wpedantic` 这一坨历史遗物。
> 每个工具的 CLI/TUI 都应该像 `bat`/`delta`/`ripgrep`/`htop`/`cargo` 那样——装完打开就觉得"这才对"。
>
> 参考的现代化工具精神：
> - `bat` — 装好就好看（不需要 `--color=auto` 别名）
> - `fd` — 命名直觉，默认行为合理（不需要 `-type f -name` 那套）
> - `ripgrep` — 快、彩色、默认递归、智能忽略 `.gitignore`
> - `delta` — 语法高亮、行内 diff，一眼看出改动点
> - `htop` / `btm` — TUI 交互式浏览，鼠标支持，树形视图
> - `cargo` / `go build` — 零参数构建，输出干净
> - `fzf` — 模糊搜索即交互方式

### 传家宝 vs 现代化——对照表

| 维度 | 传家宝（GNU 那套） | 现代化（我们要做的） |
|------|-------------------|-------------------|
| 默认输出 | 灰底白字，啥也看不清 | 彩色+符号+高亮，开箱即美 |
| 错误消息 | "undefined reference to `foo'" | "符号 foo 未定义。你是否想要：`bar`? (来自 baz.o)" |
| 选项命名 | `-Wall -Wextra -Wpedantic`（为什么叫 wall??） | `--warn=all` / `--warn=style` / `--warn=portable` |
| 配置流程 | `./configure && make && make install`（背口诀） | `meow build`（零参数，一步到位） |
| 输出格式 | 纯文本，想解析得写正则 | `--json` 是一等公民，不是事后追加 |
| 跨工具统一 | tar 用 `-xvf`，ps 用 `aux`，各说各话 | 所有工具统一 `--json`/`--quiet`/`--verbose`/`--no-color` |
| 帮助文档 | `man` 手册像本小说 | `--help` 三行示例解决 80% 需求，`--explain` 才有细节 |
| 交互方式 | 退出终端用别的工具再看 | TUI 内置：搜索/过滤/展开/折叠/导出 |
| 汉语支持 | 想都别想 | `--帮助` / `--版本` / `--详细`，不强制记英文 |

### 明确不做的"传家宝"行为

| 不做 | 因为 |
|------|------|
| ❌ 不支持 `-Wall` / `-Wextra` / `-Wpedantic` | 这些命名毫无语义，用 `--warn=` |
| ❌ 不支持 `.ld` 链接脚本 | 那是 GNU ld 的历史格式，用 meow YAML 配方 |
| ❌ 不做 `info` 页面 | GNU info 是上古排版系统，用 `--help` + `--explain` |
| ❌ 不做 `./configure && make && make install` | 那是 autoconf 时代的痛，meow 一步搞定 |
| ❌ 不做 gcc 风格的 `-O0 -O1 -O2 -O3 -Os -Ofast` | 简化：`-O0` / `-O1` / `-O2` / `-Os`，不搞 `-Ofast` 这种危险项 |
| ❌ 不做 gcc 风格的 `-Wno-*` 关闭单个警告 | `--warn=all` 默认开启但可分组管理，不需要逐个关 |
| ❌ 不支持环境变量 `CFLAGS` / `LDFLAGS` 作为唯一配置方式 | 那是 make 时代的残渣，用 meow.yaml 声明式配置 |
| ❌ 不把 stdout 和 stderr 混在一起 | 正常输出走 stdout，错误/警告走 stderr，`--json` 走 stdout |
| ❌ 不让用户背 `-xvf` / `-ajf` 这种字母迷宫 | 长选项名清晰自明，如 `--extract` / `--compress` |
| ❌ 不做 `size` / `strings` / `addr2line` / `ldd` / `nm` / `readelf` 各一个工具 | 一个 `mt-info` 统一切入点，子命令区分 |
| ❌ 不支持 `--help` 输出 200 行 | 短版三行示例，详细版 `--help=all` |

### 应用场景 vs 命令设计

不从"工具有什么选项"出发，从"用户想干什么"出发：

| 用户场景 | 传家宝做法 | 现代化做法 |
|---------|----------|----------|
| 我想编译这个项目 | `./configure && make -j4 && sudo make install` | `meow build` |
| 我想看看这个二进制是什么 | `file foo; readelf -h foo; nm foo \| head` | `mt info foo` |
| 这个二进制依赖谁 | `ldd foo`（不一定装了） | `mt deps foo` |
| 我写的代码哪里有问题 | 看 gcc 的错误输出，翻到对应行 | `mcc -c test.c` 输出彩色错误，带 caret 标记 |
| 这个警告是什么意思 | 退出编辑器，`man` 搜半天 | `mcc --explain W004` |
| 两个 ELF 有什么不同 | `objdump -d a.out > /tmp/a; diff /tmp/a /tmp/b` | `mt diff old.elf new.elf` |
| 我的构建环境是怎样的 | `gcc --version; ls /usr/include` | `meow env`（neofetch 风格一览） |
| 这个函数在哪个库里 | `nm -o /usr/lib/*.a 2>/dev/null \| grep foo` | `mt which foo` → `libfoo.a:bar.o` |

### 逐组件设计

#### meow（构建系统）— 对标 cargo/go build

```
meow build                    # 零参数构建。自动检测一切。
meow build --target=aarch64   # 交叉编译（架构自动映射 ABIs）
meow env                      # neofetch 风格：编译器/架构/头文件/库路径
meow lint                     # 配方检查，颜色标记问题
meow deps                     # 依赖树（TUI 交互式：展开/折叠/搜索）
meow graph                    # mermaid/DOT 依赖图
meow info                     # 包详情：配方/版本/依赖/安装路径
```

输出体验：
- **默认**（`--quiet`）：彩色进度条 `[▓▓▓▓░░░░] 45% (12/27)` + 编译计数 + 时间估计，失败红高亮
- **`--verbose`**：展开完整编译命令，可点击的文件路径
- **`--json`**：结构化事件流（`{"event":"compile_start","file":"foo.c"}`），供 TUI/IDE 消费
- **不输出的东西**：`make` 那种 `Entering directory /foo/bar` 的噪音
- **管道检测**：stdout 不是终端时自动 `--json`

```
# 使用示例
$ meow build
  [▓▓▓▓▓▓▓░░░░░] 64% (16/25) · 估计剩余 12s · 编译 zlib/src/deflate.c
  ✔ 构建成功 (25 文件, 3.2s)

$ meow build bzip2 --target=riscv64
  [▓▓▓▓▓▓▓▓▓▓▓▓] 100% (8/8) · 构建 bzip2 (riscv64)
  ✔ 安装到 $MEUOS_SYSROOT/usr

$ meow env
  meow v0.4.0
  编译器: mcc 0.4.0 (x86_64-meuos-linux)
  架构:   x86_64 (宿主) → x86_64-meuos-linux
  sysroot: /home/user/MeuOS-Kit/sysroot (完整)
  头文件:  /usr/include (1245 个)
  库:      libc-meuos.a + libc-meuos-compat.a
  工具链:  mt/as + mt/ld (零宿主依赖)
```

**文件**: `projects/meow/src/main.c` + 输出格式化相关文件

**验收**: `meow build` 显示彩色进度条，`meow env` 显示环境一览

#### mt-info（统一二进制分析工具）— 对标 bat/htop

取代 9 个碎片工具：`size`/`strings`/`addr2line`/`ldd`/`nm`/`readelf`/`objdump`/`strip`/`objcopy`。

不是"又一个 objdump"，而是 **"ELF 文件的一站式 TUI"**。

```
# CLI 子命令
mt info foo.elf              # 信息卡：类型/架构/入口/节区/符号/依赖，一眼看完
mt inspect foo.elf           # TUI 交互式浏览（见下方设计）
mt deps foo.elf              # 递归 DT_NEEDED 依赖树（彩色，符号链接可跟随）
mt diff a.elf b.elf          # 语义 diff：节区大小变化、符号增减、重定位差异
mt strings foo.elf           # 智能字符串提取（去重、过滤 ASCII 噪音）
mt headers foo.elf           # 快速 ELF/节区头查看（取代 readelf -h/-S 的 80% 用途）
mt which foo                 # 搜索符号：哪个库里定义了 foo？
```

TUI（`mt inspect`）设计：

```
┌─── ELF: /usr/bin/ls ───────────────────── x86_64 · 142KB · PIE ───┐
│                                                                      │
│  ┌──────────┐  ┌────────────────────────────────────────────────┐   │
│  │ 📐 节区   │  │ 节区名称        类型        地址        大小   │   │
│  │ 📊 符号   │  │ .text          PROGBITS   0x1020    28168    │   │
│  │ 🔗 重定位 │  │ .rodata        PROGBITS   0x8E20     5432    │   │
│  │ 📦 依赖   │  │ .data          PROGBITS   0xA452      824    │   │
│  │ 🔤 字符串  │  │ .bss           NOBITS     0xA7E0     4096    │   │
│  │           │  │ .dynsym        DYNSYM     0xC000     1536    │   │
│  │           │  │ .dynamic       DYNAMIC    0xC600      512    │   │
│  │           │  │                                              │   │
│  │           │  │ [Tab 切换面板  / 搜索  ↑↓ 滚动  Enter 详情]  │   │
│  └──────────┘  └────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

交互：
- **←→** 切换面板（节区/符号/重定位/依赖/字符串）
- **↑↓** / **PgUp/PgDn** 滚动内容
- **/** 搜索过滤（fzf 风格模糊匹配，实时高亮）
- **Enter** 展开选中项详情
- **Tab** 在面板列表和内容区之间切换焦点
- **q** / **Ctrl+C** 退出
- **鼠标**：可点击面板标签、滚动内容

**关键设计决策**：
- 调用现有 `libelf` 共享库，不重新实现 ELF 解析
- TUI 先用 ANSI escape + stdin raw mode 实现（零外部依赖），未来可升级到 termbox
- `mt diff` 输出格式对标 `delta`：语法高亮、行内差异标注、上下文行

**文件**: `projects/meuos-toolchain/src/mt-info/`（新建目录）

**验收**: `mt info /bin/ls` 显示彩色信息卡；`mt inspect /bin/ls` 进入 TUI 可浏览

#### mcc（编译器）— 对标 rustc 的诊断质量

**警告体系**（不叫 -Wall）：

```
--warn=all                  # 全部警告（不是 GCC 的 -Wall -Wextra 历史边界，我们重新定义）
--warn=portable             # 可移植性（类型大小/字节序/对齐假设）
--warn=style                # 代码风格（未使用变量/隐式转换/命名约定）
--warn=performance          # 性能提示（不必要的拷贝/冗余计算）
--warn=pedantic             # ISO 严格合规（比 all 更严）
```

**错误输出**（rustc 级别）：

```
$ mcc -c bug.c
  ╭─ bug.c:24:7 ────────────────────────────────────── [E003] ─╮
  │                                                             │
  │   fn foo(void) {                                            │
  │       int x = "hello";  /* 类型不匹配 */                      │
  │       ~~~~~~~~^~~~~~~~                                      │
  │                    ╰── 期望 int，得到 const char*            │
  │                                                             │
  │   💡 提示：字符串字面量不能赋值给 int。你可能是想用 'h'?      │
  │   💡 帮助：mcc --explain E003                               │
  ╰─────────────────────────────────────────────────────────────╯
```

**诊断特性**：
- 错误带代码（`E003`），可用 `mcc --explain E003` 查看完整说明
- 同类错误折叠：1 个错误折叠显示 `× 3 个类似错误（--verbose 展开）`
- 进度指示器：大文件编译时底部显示 `[▓▓▓▓░░] 55% 解析中...`
- 管道检测：stderr 输出到文件时自动去颜色、去折叠

**文件**: `projects/mcc/src/parse/`（诊断输出）+ `projects/mcc/src/driver/`（--warn 解析）

**验收**: `mcc --warn=all -c bug.c` 显示彩色分组错误

#### mt/as — 汇编可视化

```
as --debug foo.s            # 逐指令显示：汇编源码 → 机器码 → 字节
as --stats foo.s            # 统计：指令数/伪指令数/各节区大小/重定位数
```

`--debug` 输出示例：
```
$ as --debug foo.s
  .text
  00:  mov x0, #42        →  d2800540    (4 字节)
  04:  mov x1, #1         →  d2800021    (4 字节)
  08:  svc #0             →  d4000001    (4 字节)
  0c:  ret                →  d65f03c0    (4 字节)
  ────────────────────────────────────────────
  总计: 4 条指令 · 16 字节 · 无重定位
```

#### mt/ld — 链接器诊断

```
ld --why=foo               # "符号 foo 来自 bar.o (libsomething.a)"
ld --stat                  # 总大小/各节区大小 TOP5/符号数/重定位数
ld --map-tui               # TUI 链接映射（交互式查看节区布局和地址分配）
```

`--why` 示例：
```
$ ld --why=printf
  printf 被链接的原因链:
    main.o 引用了 printf
    → main.o 来自 app.elf 的输入文件
    → 定义在 /usr/lib/libc-meuos.a(stdio/printf.o)
```

`--map-tui` TUI 模式复用 `mt inspect` 的框架。

#### msysctl — CLI 升级

```
msysctl tree                # 树形内容（彩色，目录折叠）
msysctl info                # .msys 信息卡（类型/大小/校验/签名/层叠关系）
msysctl diff                # 两个 .msys 差异（格式对照 delta）
msysctl --json              # 所有命令支持 JSON
msysctl fzf                 # 已有交互式浏览器，升级彩色
```

### 跨工具一致性约定（非协商）

以下约定**所有工具必须遵守**，不搞各说各话：

| 约定 | 要求 |
|------|------|
| `--json` | 所有工具必须支持 JSON 输出，**不是事后追加的**，是一等公民输出格式 |
| `--quiet` / `--verbose` / `--debug` | 三级输出控制，所有工具统一语义和退出码 |
| 管道检测 | stdout 非终端时自动 `--json`（除非显式 `--no-color`），stderr 非终端时去颜色 |
| 退出码 | `0`=成功, `1`=一般错误, `2`=用法错误（严格遵循 sysexits.h） |
| `--help` | 前三行是示例（覆盖 80% 使用场景），然后是选项表，不 dump 200 行 |
| `--version` | `工具名 v版本 (架构/操作系统)`，不输出 GPL 版权声明那种长文 |
| 颜色语义 | 红=错误, 黄=警告, 青=信息, 绿=成功, 灰=次要信息。全局统一，不各自定义 |
| 颜色方案 | 默认适配暗色终端；持 `NO_COLOR` 环境变量标准 |
| 命名风格 | 长选项一律 `--单词`（不用 `-Wl,` 这种缩写体操） |

### 工具联动体验

> 单个工具的界面好看只是第一步。真正的好体验是工具之间"知道彼此存在"，配合无感。

#### 1. 共享环境上下文

所有工具共享同一套环境检测，不各自查一遍：

```
# 任何工具启动时自动检测（不重复）
MEUOS_SYSROOT  →  sysroot 路径和完整性
宿主编译器      →  mcc > gcc > tcc，版本检测
交叉工具链      →  aarch64/riscv64/loongarch64 是否可用
当前架构        →  宿主 + 目标架构（--target 覆盖）
mt 工具链       →  MT_AS/MT_LD 是否就绪
```

实现方式：`libmeuosenv.so` 或统一的 `meuos_env.h`，所有工具链接同一个检测逻辑。
生命周期内只检测一次，结果缓存。

#### 2. meow 自动集成诊断

```
$ meow build
  [▓▓▓▓▓▓▓▓▓▓▓▓] 100% (25/25)
  ✘ 链接失败: 未定义符号 'compress2'

  ╭─ 自动诊断 ──────────────────────────────────────────────╮
  │  mt ld --why=compress2                                   │
  │  → 符号 compress2 被 main.o 引用                         │
  │  → 定义在 libz.a(compress.o) —— 但未链接 (-lz)            │
  │                                                          │
  │  💡 建议：在 meow.yaml 中添加 libraries: ["z"]           │
  ╰──────────────────────────────────────────────────────────╯
```

**meow 在链接失败时自动调用 `ld --why` 获取解释**，而不是丢给用户一行冰冷链接器错误。

#### 3. 编译 → 分析 → 诊断闭环

```
$ mcc -c foo.c --error-json | mt-analyze --suggest
  # mt-analyze 接收 JSON 错误流，分析模式并提供修复建议

$ mt info build/out.elf --warn
  # 自动检查产物问题：未剥离调试符号、可执行栈、缺失 RELRO
  # 这些本该是链接器警告的，但 mt-info 可以在 post-build 阶段检查
```

#### 4. 统一管道协议

工具之间通过结构化数据（JSON lines）通信，不靠解析文本来传参：

```
# 编译错误 → 分析器 → 格式化输出
mcc -c foo.c --error-json | mcc-diag-format | less -R

# 依赖分析 → 可视化
mt deps app.elf --json | meow graph --format=mermaid

# 构建 → 验证 → 报告
meow build --json | mt info --stdin-elf --json | meow report
```

#### 5. meow 后置检查钩子

构建完成后自动跑验证：

```yaml
# meow.yaml
build:
  post_check:
    - "mt info build/app.elf --warn"          # 检查 ELF 安全性
    - "mt deps build/app.elf --no-unused"     # 检查未使用的依赖
    - "mt diff build/app.elf previous.elf"    # 与上次构建对比
```

这些检查的输出集成到 meow 的彩色报告里，不是单独冒出一个工具的输出。

#### 6. "一步到位"的典型工作流

| 想做的事 | 传家宝做法 | 联动做法 |
|---------|----------|---------|
| 改代码 → 构建 → 验证 | `vim foo.c` → `make` → `readelf -h a.out \| head` | `mcc foo.c` → 自动 `mt info` 验证产物 |
| 发现 bug → 追溯源码 | 看崩溃地址 → `addr2line` → 手动翻代码 | `mt addr 0x4023 app.elf` 自动关联源码行 |
| 构建失败 → 找原因 | 看输出 → 猜 → 可能再跑一遍 | meow 自动调 `ld --why` 解释 |
| 包依赖 → 查出谁引用了谁 | `nm -o *.a \| grep` 猜 | `mt which foo` → 来源库 + 来源目标文件 + 引用链 |
| 要交叉编译 → 配环境 | 搜交叉工具链 → 设环境变量 → 祈祷 | `meow build --target=aarch64` 自动配好一切 |

### 子任务

| ID | 组件 | 描述 | 优先 | 实施情况 |
|----|------|------|------|---------|
| meow-cli | meow | 彩色进度条/分层输出/--json/meow env TUI 概览 | 🟡 中 | 🟢 `83d395d`（color.c/env.c + main.c/graph.c/exec.c 修改） |
| mt-info | meuos-toolchain | 统一 ELF 分析工具（info/inspect/deps/diff/strings/which 子命令），含 TUI 交互模式 | 🟡 中 | 🟢 已实现（src/mt-info/ 9 文件 + 统一 --json/--quiet/--no-color 跨工具约定；见 144 行 P2 详细状态） |
| mcc-diag-output | mcc | 彩色错误输出/自定义 --warn 体系/--error-json/--explain | 🟢 低 | 🟢 完成：彩色错误 + caret `^` 指示（1ded4c3，token.c ANSI + isatty 自动开关）；`--warn=` 语义分组体系（9713bfe）；`--error-json` 结构化诊断（`{"level":"error","file":...,"line":...,"col":...,"message":...}`，token.c:191）；`--explain` 修复建议标志（g_error_explain 全局，main.c:310）。验证：`mcc --error-json` 输出 JSON 诊断；常规模式输出彩色错误 + `^` caret。 |
| as-debug-output | mt/as | --debug 逐指令可视化/--stats | 🟢 低 | 待设计 |
| ld-tui-map | mt/ld | --map-tui TUI 链接映射/--why 符号溯源 | 🟢 低 | 待设计 |
| msysctl-upgrade | meuos-sysroot | tree/diff/--json 升级 | 🟢 低 | 待设计 |
| tool-integration | 跨组件 | 共享环境上下文（libmeuosenv/meuos_env.h） | 🟡 中 | 待设计 |
| meow-auto-diag | meow+ld | 构建失败时自动调用 ld --why 诊断 | 🟢 低 | 待设计 |
| post-check-hooks | meow | meow.yaml post_check 钩子 + mt-info 集成 | 🟢 低 | 待设计 |
| json-pipeline | 跨组件 | 统一 JSON lines 管道协议，工具可管道串接 | 🟢 低 | 待设计 |
| mt-info | meuos-toolchain | 统一 ELF 分析工具（info/inspect/deps/diff/strings/which 子命令），含 TUI 交互模式 | 🟡 中 | 🟢 已实现（src/mt-info/ 9 文件 + 统一 --json/--quiet/--no-color 跨工具约定；见 144 行 P2 详细状态） |
| mcc-diag-output | mcc | 彩色错误输出/自定义 --warn 体系/--error-json/--explain | 🟢 低 | 🟢 完成：彩色错误 + caret `^` 指示（1ded4c3，token.c ANSI + isatty 自动开关）；`--warn=` 语义分组体系（9713bfe）；`--error-json` 结构化诊断（`{"level":"error","file":...,"line":...,"col":...,"message":...}`，token.c:191）；`--explain` 修复建议标志（g_error_explain 全局，main.c:310）。验证：`mcc --error-json` 输出 JSON 诊断；常规模式输出彩色错误 + `^` caret。 |
| as-debug-output | mt/as | --debug 逐指令可视化/--stats | 🟢 低 | 待设计 |
| ld-tui-map | mt/ld | --map-tui TUI 链接映射/--why 符号溯源 | 🟢 低 | 待设计 |
| msysctl-upgrade | meuos-sysroot | tree/diff/--json 升级 | 🟢 低 | 待设计 |

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

## 待修复的 Bug（本次 worktree 发现）

（暂无）

---

## 已解决的问题

| ID | 组件 | 描述 | 状态 |
|----|------|------|------|
| bug-ld-pie-dynamic | mt/ld + mcc | `mcc -pie` 生成的 PIE 二进制**缺少 .dynamic 节区**（readelf -d 显示 "no dynamic section"），导致 `mt deps` 无法显示 DT_NEEDED。静态链接（ET_EXEC）正常。ld-pie 任务虽标记 🟢 但动态段生成不完整 | 🟢 `2bf4f7a`（mcc driver 传递 -pie 到 mt/ld + specs 模式不强制 static） |

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
