# MeuOS Kit - Agent 初始化 Prompt

> Update: 2026-07-26
>
> IMPORTANT: 全程思考/回复/文档优先使用简体中文
> [你必须称呼用户为大喵 (she/her) ]
>
> **分支与提交策略**：
> - **新任务必须创建分支**，禁止在 `main` 上直接开发（格式：`feat/<描述>`、`fix/<描述>`、`doc/<描述>`）。
> - **worktree 分支**（`worktree-<描述>`）用于长期、跨多组件的联动开发（如 `worktree-mcc-libc-work`）。worktree 分支的特点：
>   - 生命周期长，可累积多个组件的修改
>   - 阶段性成果应合并到 `main` 后再继续下一阶段
>   - 提交时仍需遵守单组件粒度（`<组件>: <描述>`）
>   - 合并到 `main` 前应做全量回归（对应组件 `make check`）
> - 仅在以下情况可提交到 `main`：一次性修复（如 typo、编译报错修正）、文档同步、`.issues/` 状态更新、纯重构不涉及功能变更。
> - 每次提交前必须跑对应组件的 `make check`，确保不引入回归。
> - 提交信息格式：`<组件>: <描述>`，例如 `mcc: fix va_list alignment on i386`。
> - **完成即推送**：每次提交后直接 `git push`，不积压本地提交。
> - **合后清理**：分支合并到 `main` 后，若无特殊要求，删除本地分支（`git branch -d`），保留远程分支。
>
> **会话恢复流程**（强制要求：新 agent 启动时**必须**按顺序执行以下步骤）：
> 1. **读取 IMA 知识库规划文档** — Agent 启动后**必须主动**查询 IMA 知识库中的 MeuOS 规划文档
>    （`search_knowledge_base` → `get_knowledge_list`），阅读所有标题含"规划"/"计划"/"路线图"/
>    "需求"/"设计"的文档。这些文档包含当前阶段的需求、设计方案和任务计划，是理解"接下来做什么"
>    的第一信息来源。详见 §9.4。
> 2. **子项目上下文加载** — 读目标子项目的 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.issues/`
>    （待实现项），了解项目当前进度。
> 3. **AGENTS.md 规约确认** — 重新确认项目规约（§4 禁止事项、§7 任务编排策略）和当前状态速查（§10）。
> 4. **环境检查** — 确认 `MEUOS_SYSROOT` 已设置，宿主编译器和交叉工具链可用。
>
> 各子项目独立维护状态，无全局 STATE 文件。`.issues/` 和 ARCHITECTURE.md 是状态权威来源。
> **待办任务约定**：所有待办任务统一存放在 `.issues/` 下，以日期编号命名（如 `0729.md`、`0730.md`）。禁止在项目目录下创建 `.todo/` 子目录或散落的待办文件。

**项目名称**：MeuOS Kit
**项目定位**：MeuOS Next 的完整自举开发工具集。提供从零自举所需的全部工具：C/C++ 编译器、标准 C 库、构建系统、底层工具链、核心工具集与 Shell。
**许可**：RFL (Run Free Software License) v1.0

**核心组件**：

- `meuos-libc` - 标准 C 库（ISO C11 + POSIX，零 GNU 依赖；compat 层独立归档）
- `mcc` / `m++` - 编译器（C99+C11 完整实现 + C23 稳定实现；后续 C++ 共享后端）
- `meow` - 构建系统（取代 make + autoconf，MeuOS 中不依赖 make）
- `meuos-toolchain` - 底层工具链（as/ld/ar/ranlib，完全取缔 binutils）
- `meuos-sysroot` - 单文件 sysroot 系统（.msys 格式，mcc/mt/meow 原生读取，无需解压到文件系统）
- `meuos-utils` - 核心工具集（coreutils/diffutils/findutils 完整替代）
- `meuos-shell`（msh）- Shell 终端（完整 Shell，可选 bash 兼容与 zsh 插件/主题）
- `meuos-buildtools` - 构建工具（m4/bison/flex/gperf，取代 GNU 构建工具链依赖）

**交付对象**：具备系统编程和编译器经验的大型 AI Agent（兆级上下文）。

---

## 1. 项目目标

构建一套完整的开发工具集，使得：

1. 可以从任意 Linux 宿主（有 gcc 或 tcc）自举出全套 MeuOS Kit 工具。
2. 用自举出的 Kit 工具能够编译出 MeuOS Next 的最小 sysroot。
3. Kit 自身可以在 MeuOS Next 环境中自我重建（自举验证通过）。
4. 整个自举链零 GNU 代码、零 LLVM 代码、零 glibc 依赖。
5. MeuOS Next 环境中不依赖任何 GNU 工具（make/autoconf/binutils/coreutils 等）。

---

## 2. 组件规范

### 2.1 meuos-libc - 标准 C 库

**标准支持**：ISO C11 + POSIX.1-2008 核心接口。核心库是标准化的干净实现，利用 `_Atomic`、`_Generic`、`_Thread_local` 等 C11 特性。

**系统调用**：直接封装 Linux 内核 ABI，使用 `syscall()` 或内联汇编，不经过任何中间库。每个系统调用一个独立源文件。

**符号策略**：核心库只暴露标准符号（ISO C + POSIX）。任何 glibc 扩展符号（`error_at_line`、`obstack`、`argp`、`getline` 等）放入 `meuos-libc/src/compat/`，作为独立归档 `libc-meuos-compat.a` 提供。compat 层是兼容实现，核心库不包含任何非标准符号。

**实现原则**：可以参考 musl 的算法和结构，但必须用自己的代码重新实现，不直接复制 musl 源码。所有实现必须能用 `mcc` 编译（初期 C99 风格，逐步迁移到 C11）。

---

### 2.2 mcc / m++ - 编译器

**架构**：**源码级整合** cproc 编译器前端 + QBE 编译器后端。单体可执行文件，不区分前后端。cproc 的语义阶段直接调用 IR 构造 API，无文本 IR 序列化。所有模块共享统一的内存管理、错误报告、符号表。

**C 语言目标**：

- **C99**：完整实现
- **C11**：完整实现（`_Atomic`、`_Generic`、`_Thread_local`、`_Alignas`、`_Alignof`、`_Noreturn`、`_Static_assert`、匿名结构体/联合体、复合字面量、指定初始化器、变长数组）
- **C23**：完整实现（`constexpr`、`typeof`/`typeof_unqual`、`nullptr_t`、`#embed`+`limit(N)`/`prefix`/`suffix`/`if_empty`、`__has_include`、属性语法 `[[]]`、`#elifdef`/`#elifndef`、`#warning`、二进制字面量`0b`/数字分隔符`'`、空初始化器`{}`、`auto`类型推导、Labeled break/continue、`bool`/`true`/`false`关键字、`_BitInt(N)`、`_Decimal32`/`64`/`128`、`static_assert`无消息形式）

**C++ 目标（m++）**：C 语言功能稳定后启动。m++ 复用 mcc 的后端（IR/指令选择/寄存器分配/汇编输出），通过 `libmcc` 共享后端库实现。m++ 前端独立实现 C++ 语法/语义。

**命令行**（gcc/clang 风格）：

```
mcc -o <output> <files...> [options]
options:
  -I<dir>             头文件搜索路径
  -L<dir>             库搜索路径
  -l<lib>             链接库
  --static            静态链接（兼容: -static）
  --shared            生成共享库（兼容: -shared）
  --sysroot=<dir>     系统根目录
  -D<macro>           预定义宏
  -U<macro>           取消宏定义
  -O<level>           优化级别（0-2）
  -g                  生成调试信息
  --nostdinc          不搜索标准头文件（兼容: -nostdinc）
  --nostdlib          不链接标准库（兼容: -nostdlib）
  --specs=meuos       使用 MeuOS 默认配置（兼容: -specs=meuos）
  -c                  编译到 .o，不链接
  -S                  编译到 .s 汇编
  -E                  只预处理
  --target=<triplet>  目标三元组（兼容: -target=）
  -v                  verbose
  --version           打印版本信息
  --help              打印帮助
```

**默认行为**：`--specs=meuos` 是默认模式，自动设置 sysroot、头文件路径、库路径，链接 `libc-meuos`。

**自举要求**：mcc 必须能用自己编译自己。m++ 同理（实现后）。

---

### 2.3 meow - 构建系统

**目标**：取代 make + autoconf。MeuOS Next 环境中不依赖 make 构建。

**配方格式**：YAML。一个包的完整构建描述。

**Makefile 兼容**：如果当前目录下没有 `meow.yaml`，meow 自动检测 `Makefile`/`GNUmakefile` 并透明调用 `make`。此模式仅用于过渡，移植完成后 MeuOS 包应全部使用原生 YAML 配方。

**取代 autoconf**：meow 内置特性检测能力（编译测试、头文件/库存在性检测），生成 `config.h`，不需要 `configure` 脚本。YAML 配方中声明依赖和检测规则，meow 执行检测并生成配置。

**取代 pkg-config**：meow 内置包查询，记录已安装库的编译/链接参数（`CFLAGS`/`LDFLAGS`），配方中通过变量引用。

**取代 libtool**：meow 直接管理共享库/静态库的构建规则，不需要 `.la` 文件抽象。

**内置归档/补丁**：meow 的 `fetch`/`unpack` 步骤内置 tar/gzip/bzip2/xz 解压和 patch 应用能力，不依赖外部命令。

**自身构建**：用纯 C 编写，依赖 meuos-libc。支持依赖 DAG、增量构建、模式规则、并行构建（`-jN`）。

**命令**：

```
meow build <package>     # 构建指定包
meow clean <package>     # 清理
meow list                # 列出可用包
meow --bootstrap         # 自举模式：用宿主编译自己
```

---

### 2.4 meuos-toolchain - 底层工具链

**目标**：完全取缔 GNU binutils。提供从汇编到链接到二进制分析的完整底层工具链。

**工具清单**：

| 工具        | 功能                   | 取代        |
| ----------- | ---------------------- | ----------- |
| `as`      | 汇编器（.s -> .o）     | gas         |
| `ld`      | 链接器（.o/.a -> ELF） | GNU ld      |
| `ar`      | 归档器（.o[] -> .a）   | GNU ar      |
| `ranlib`  | 归档索引生成           | GNU ranlib  |
| `nm`      | 符号列表               | GNU nm      |
| `objdump` | 反汇编和节区查看       | GNU objdump |
| `readelf` | ELF 结构查看           | GNU readelf |
| `strip`   | 删除调试/非必要符号    | GNU strip   |
| `objcopy` | 节区和格式复制         | GNU objcopy |

**设计原则**：不包含宿主 `<elf.h>`，所有 ELF 常量自带。代码零 GNU/binutils 依赖。内部共享 `libelf` 库。

**路线图**：P0-P4 已完成（x86_64 静态链接+汇编器+ar/ranlib+二进制辅助工具+mt/ld 集成），P5-P11 详见 `projects/meuos-toolchain/ARCHITECTURE.md`。

自举链零宿主依赖已验证（`check-mt-integration` 通过）：mcc driver 通过 `MT_AS`/`MT_LD` 环境变量集成 mt 工具链，消除对宿主 `cc` 的最后依赖。P4 辅助工具（readelf/nm/objdump/strip/objcopy）也已实现。

---

### 2.5 meuos-utils - 核心工具集

**目标**：提供 coreutils/diffutils/findutils 的完整替代，兼容 GNU 系列工具套件。参考 Rust 的 uutils 项目设计。

**工具范围**：

| 类别      | 工具                                                                                                                                                                                                      |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| coreutils | `ls`/`cp`/`mv`/`rm`/`cat`/`echo`/`mkdir`/`rmdir`/`touch`/`ln`/`chmod`/`chown`/`wc`/`head`/`tail`/`sort`/`uniq`/`cut`/`tr`/`tee`/`dd`/`df`/`du`/`which` 等 |
| diffutils | `diff`/`cmp`/`patch`                                                                                                                                                                                |
| findutils | `find`/`locate`/`xargs`                                                                                                                                                                             |
| 文本处理  | `grep`/`sed`/`awk`                                                                                                                                                                                  |
| 归档      | `tar`（创建/解包，支持 gzip/bzip2/xz/zstd 格式透传）                                                                                                                                                    |
| 压缩      | `gzip`/`bzip2`/`xz`/`zstd`/`unzip`                                                                                                                                                              |

**设计原则**：

- 用 C 编写（mcc 编译），依赖 meuos-libc
- 兼容 GNU 对应工具的命令行选项和行为（`--help`/`--version`）
- 可选的多调用二进制（multi-call binary，类似 BusyBox）
- 独立项目，不依赖 GNU coreutils 源码

---

### 2.6 meuos-shell (msh) - Shell 终端

**目标**：提供完整的 Shell 终端，满足 MeuOS Next 日常使用和脚本编程需求。

**功能层次**：

1. **POSIX sh 兼容**：完整实现 POSIX.1-2008 Shell 命令语言
2. **交互式 Shell**：行编辑、历史、Tab 补全、作业控制
3. **可选 bash 兼容**：bash 脚本兼容模式（`#!/bin/bash` 脚本可运行）
4. **可选 zsh 插件/主题**：插件系统、主题引擎（参考 zsh 的 oh-my-zsh 生态）

**设计原则**：

- 用 C 编写（mcc 编译），依赖 meuos-libc
- 独立项目，不复制 bash/zsh 源码
- 模块化设计：核心 Shell 引擎 + 可选的 bash 兼容层 + 可选的插件/主题系统

---

### 2.7 meuos-buildtools - 构建工具

**目标**：提供构建真实软件包时所需的代码生成和宏处理工具，取代 GNU 构建工具链依赖。

**工具清单**：

| 工具                    | 功能                         | 取代        | 典型依赖场景                                  |
| ----------------------- | ---------------------------- | ----------- | --------------------------------------------- |
| `m4`                  | 宏处理器                     | GNU m4      | autoconf 的 configure 生成、 Bison 输出后处理 |
| `bison`               | 解析器生成器（.y -> .c/.h）  | GNU Bison   | binutils, bash, gawk, flex 等的语法分析       |
| `flex`                | 词法分析器生成器（.l -> .c） | Flex        | binutils, bash, gawk, wc 等的词法扫描         |
| `gperf`               | 完美哈希函数生成器           | GNU gperf   | glib, libidn2 等的关键字查找                  |
| `msgfmt`/`msgmerge` | i18n 翻译编译器              | GNU gettext | 构建需要 i18n 的软件包（.po -> .mo）          |
| `pkg-config`          | 包查询工具                   | pkg-config  | 查询已安装库的编译/链接参数                   |

**设计原则**：

- 用 C 编写（mcc 编译），依赖 meuos-libc
- 兼容 GNU 对应工具的命令行选项、输入语法和输出格式
- 独立项目，不复制 GNU 源码
- 输出必须能被 mcc 正确编译

**与 meow 的关系**：meow 负责调度构建流程，meuos-buildtools 提供被调用的工具。meow 的 YAML 配方中 `bison`/`flex` 等命令直接调用这些工具。

**自举顺序**：在 Phase 5（工具链完善）之后、Phase 6（用户空间）之前构建。部分软件包（如 bash、binutils）的构建需要这些工具。

---

## 2.8 软件包策略：Kit 实现 vs meow 构建

参考 LFS 12.1 软件包列表，分为两类：

### Kit 实现（自举链基础设施）

Kit 组件是构建其他软件的基础设施，必须由 Kit 自己实现，不依赖 GNU 对应物：

| LFS 软件包                    | Kit 组件                                   | 状态                   |
| ----------------------------- | ------------------------------------------ | ---------------------- |
| Glibc                         | meuos-libc                                 | ✅                     |
| GCC                           | mcc/m++                                    | ✅ C11，✅ C23，m++ 待 |
| Binutils                      | meuos-toolchain                            | ✅ P0-P4（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump + mt 集成） |
| Make                          | meow                                       | ✅                     |
| M4/Bison/Flex/Gperf           | meuos-buildtools                           | 待启动                 |
| Coreutils/Diffutils/Findutils | meuos-utils                                | 待启动                 |
| Gawk/Sed/Grep                 | meuos-utils（文本处理）                    | 待启动                 |
| Bash                          | meuos-shell (msh)                          | 待启动                 |
| Autoconf/Automake/Libtool     | 被 meow 取代（特性检测/包查询/库构建内置） | 内置                   |
| CMake/Ninja                   | 被 meow 取代（并行构建/多平台内置）        | 内置                   |

### meow 软件包（从源码构建，不自己实现）

这些是应用级库和软件，用 Kit 工具从源码编译安装，通过 `meow build <package>`：

| 类别     | 软件包                        | 说明                                                        |
| -------- | ----------------------------- | ----------------------------------------------------------- |
| 数学库   | GMP, MPFR, MPC                | GCC 的依赖，mcc 不需要；Python/加密库等需要时通过 meow 构建 |
| 数据库   | GDBM, Berkeley DB             | 应用级存储库                                                |
| 压缩     | Zlib, Bzip2, Xz, Zstd         | 基础依赖库                                                  |
| 终端     | Ncurses, Readline             | msh 可自实现行编辑；其他软件需要时通过 meow 构建            |
| 正则     | Pcre2                         | grep 可用 POSIX 正则；需要 PCRE 的软件通过 meow 构建        |
| 加密     | OpenSSL/LibreSSL, GnuPG       | 安全库                                                      |
| 解释器   | Python, Perl                  | 脚本语言                                                    |
| 系统工具 | Util-linux, Procps, E2fsprogs | 系统管理工具                                                |
| init     | —                            | MeuOS Next 自有 init，不用 systemd/sysvinit                 |

**判断原则**：

- 自举链直接需要的工具 → Kit 实现
- MeuOS Next 中不可依赖 GNU 的工具 → Kit 实现
- 应用级库和软件 → meow 软件包（用 Kit 工具从源码构建）
- 被取代的工具（make/autoconf/cmake）→ 不实现、不构建

---

### 2.9 mz - 压缩库

**目标**：提供轻量 LZ77 压缩/解压缩 API，作为构建工具（如 .msys 的压缩模式）的基础依赖。

**接口**：
```c
int mz_compress(const void *in, size_t il, void **r, size_t *rl, int c, int lv);
int mz_decompress(const void *in, size_t il, void **r, size_t *rl, int c);
size_t mz_max_compressed_size(size_t il, int c);
const char *mz_strerror(int e);
```

**构建**：
```sh
make -C projects/meuos-compress              # 构建 libmz.a
make -C projects/meuos-compress check        # 压缩/解压缩轮转测试
```

**实现原则**：纯 C11 实现，零外部依赖。当前仅实现 LZ77 编码器（`MZ_CODEC_LZ77`），后续可扩展其他编解码器。

---
## 3. 自举流程

### 3.1 组件间构建依赖

组件之间的构建依赖关系（从下往上依赖，下层必须先构建）：

```
meuos-buildtools (m4/bison/flex/gperf)
  ↑ 依赖 mcc + meuos-libc
meuos-utils / meuos-shell
  ↑ 依赖 mcc + meuos-libc + meow
meow（构建系统）
  ↑ 依赖 mcc + meuos-libc + meuos-toolchain（mt/as, mt/ld）
meuos-toolchain (as/ld/ar/ranlib/nm/objdump/readelf/strip/objcopy)
  ↑ 依赖 mcc + meuos-libc + meuos-sysroot（libmsys）
meuos-sysroot（.msys 格式：libmsys + mkmsys + msysctl）
  ↑ 依赖宿主 cc（Phase 0-1）或 mcc（Phase 4+）
meuos-libc（标准 C 库）
  ↑ 依赖 mcc 编译（Phase 2+）
mcc（编译器）
  ↑ 依赖宿主 cc（Phase 1）或自身（Phase 4 自举）
```

### 3.2 自举阶段

Agent 必须严格遵循以下阶段，每步都要验证：

**Phase 0 - 准备**

- 宿主编译器可用（gcc 或 tcc）。
- 设定 `MEUOS_SYSROOT` 环境变量指向目标根文件系统路径。

**Phase 1 - 诞生 mcc**

- 用宿主编译器编译 mcc 源码，产出第一代 `mcc` 二进制。
- 验证：`mcc` 能编译 `int main(){return 0;}` 并输出可执行文件。

**Phase 2 - 诞生 meuos-libc**

- 用 Phase 1 的 `mcc` 编译 meuos-libc（含 compat 层）。
- 安装到 `${MEUOS_SYSROOT}/lib` 和 `${MEUOS_SYSROOT}/include`。

**Phase 3 - 诞生 meow**

- 用 `mcc` + `meuos-libc` 编译 meow。
- 验证：`meow build` 能读取 YAML 配方并执行。

**Phase 4 - 自举验证**

- 用 sysroot 内的 `mcc` + `meow` 重新编译 mcc、meuos-libc、meow。
- 比较两次产物的行为一致性（功能等价即可，不要求 bit 级相同）。

**Phase 5 - 工具链完善**

- 用 `mcc` + `meuos-libc` 构建 `meuos-toolchain`（as/ld/ar/ranlib）。
- mcc driver 集成 mt 工具，消除对宿主 `cc` 的最后依赖。
- 验证：Kit 全程零宿主依赖。

**Phase 6 - 构建工具**

- 构建 `meuos-buildtools`（m4/bison/flex/gperf）。
- 验证：能用 meow + buildtools 构建需要 bison/flex 的软件包。

**Phase 7 - 用户空间**

- 构建 `meuos-utils`、`meuos-shell`。
- 验证：MeuOS Next 最小 sysroot 可运行、可交互。

---

## 4. 禁止事项（强约束）

- **禁止**任何 glibc 专有头文件、符号、宏出现在 meuos-libc 核心或 mcc 源码中。
- **禁止**引入 LLVM/Clang 或 GCC 的任何代码。
- **禁止**使用 autotools、cmake、meson 作为 Kit 自身的构建系统（Kit 自身组件必须用简单 Makefile 或 shell 脚本构建）。
- **禁止**系统调用通过 libc 封装，必须直接 `syscall()` 或内联汇编。
- **禁止**预编译二进制提交到仓库（宿主 bootstrapper 除外）。
- **禁止**在 MeuOS Next 环境中依赖 GNU 工具（make/autoconf/binutils/coreutils/bash/m4/bison/flex 等）。
- **要求**构建可重现（无时间戳、无绝对路径硬编码）。

---

## 5. 项目组织

### 5.1 目录结构

```
MeuOS-Kit/
├── AGENTS.md               项目规约（本文件，harness 自动加载）
├── README.md               项目说明与构建方法
├── bootstrap.sh            Phase 0–5 全流程自举脚本
├── cron.md                 循环任务定义（session 级，随会话结束清理）
├── issue/                  全局 issue 追踪（按日期命名的详细待实现/缺陷分析清单）
├── .issues/                当前 worktree 的任务队列与入口文档（worktree 活跃期间有效）
├── projects/
│   ├── mcc/                C/C++ 编译器（C99+C11+C23；m++ 待启动）
│   ├── meuos-libc/         标准 C 库（ISO C11 + POSIX；含 compat 兼容层）
│   ├── meow/               构建系统（取代 make + autoconf）
│   ├── meuos-toolchain/    底层工具链（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump）
│   ├── meuos-sysroot/      .msys 单文件 sysroot 系统（libmsys + mkmsys + msysctl CLI + Python 绑定，已集成到 mcc）
│   ├── meuos-utils/        核心工具集（待启动）
│   ├── meuos-shell/        Shell 终端（待启动）
│   ├── meuos-buildtools/   构建工具（m4/gperf/flex/bison）
│   └── meuos-compress/       压缩库（libmz.a，LZ77）
├── env/                    QEMU 多架构测试环境（6.6.142 内核 + 9p 共享）
│   ├── bin/qvm             VM 管理器
│   ├── qemu/               静态 qemu-user 二进制（aarch64/riscv64/loongarch64）
│   ├── kernels/<arch>/     Alpine linux-virt 6.6.142 内核
│   └── rootfs/             initramfs 镜像
├── pkgs/                   meow 构建配方（YAML；dash/bzip2/binutils 等）
├── sysroot/                安装目标根文件系统（默认 MEUOS_SYSROOT）
└── reference/              cproc/QBE/musl 只读参考源（gitignored）
```

每个组件目录含 `ARCHITECTURE.md`（结构/模块/状态/路线图）。待办事项统一存放在 `.issues/` 下。

**配方包（pkgs/）**：`pkgs/` 存放 `.meow` 格式构建配方，涵盖基础依赖库（dash/bzip2/binutils）、meow 自测试配方（`meow-smoke`、`meow-incremental` 等）和 Kit 组件配方（`mcc`、`meow`、`meuos-libc`）。通过 `meow build <pkg>` 使用，详见 `pkgs/<pkg>/project.meow`。

**sysroot 多架构布局**：`sysroot/` 下按架构分目录，支持多架构同时安装：
```
sysroot/
├── x86_64/        # 默认架构（ARCH= 缺省值）
├── aarch64/
├── arm/
├── i386/
├── loongarch64/
└── riscv64/
```
跨架构安装时指定 `ARCH=<arch>`，如 `make -C projects/meuos-libc ARCH=aarch64 install`。

### 5.2 构建约定

- 每个组件用**简单 Makefile** 构建（§4 禁止 autotools/cmake/meson）。
- 编译产物放入独立输出目录（通常是 `build/`），不污染源码树。
- `MEUOS_SYSROOT` 环境变量控制安装目标路径（默认 `<repo-root>/sysroot`）。
- 跨架构时 `ARCH=<arch>` 切换目标（x86_64/aarch64/riscv64/loongarch64/i386）。
- 每个 `.S` 文件通过宿主 cc 或交叉 gcc 汇编（mcc 不处理内联汇编指令）。

### 5.3 QEMU 测试环境

`env/` 提供基于 Alpine Linux 6.6.142 内核的 QEMU 测试环境，支持完整系统仿真和
单 ELF 运行时验证：

- **qemu-system VM**（完整系统仿真）：x86_64 / i386 / aarch64 / riscv64 / loongarch64
- **qemu-user**（单 ELF 运行时验证）：aarch64 / riscv64 / loongarch64 静态二进制可用
- **qvm 管理器**（`env/bin/qvm`）：`qvm boot|console|run|stop <arch>`
- **9p 共享**：宿主 `share/` 挂载到 guest 的 `/mnt/host`

详见 `env/README.md` 和 §8.1 命令。

---

## 6. 实现策略

### 6.1 实现路径与特化原则

**先标准化可用，再增强优化。** 所有组件遵循三阶段实现路径：

1. **标准化可用**：实现 ISO/POSIX 标准定义的接口，确保正确性和完整兼容性。MeuOS 的目标是真实可用的成熟发行版，对外的标准兼容性不妥协、不阉割。
2. **有利特性**：在标准兼容基础上，增加 MeuOS Next 实际需要的便利特性（便捷命令行选项、扩展 API 等），作为标准接口的补充而非替代。
3. **性能优化**：在功能和接口稳定后，针对 MeuOS 的实际工作负载做性能优化。

**组件间特化**：Kit 的所有组件都是自己实现的，架构上相互知晓彼此的内部逻辑。组件之间不必通过通用兼容性接口通信，而是可以利用内部知识做特化实现：

- **mcc -> mt/as**：mcc 知道 mt/as 支持的编码子集和偏好，直接生成最优汇编，无需覆盖 gas 全语法
- **meow -> mcc**：meow 知道 mcc 的编译速度和依赖特性，可以做针对性的并行调度和增量构建
- **libc -> mcc**：libc 知道 mcc 的 ABI 约定和优化能力，可以直接配合编写高效实现
- **mt/ld -> libc**：ld 知道 libc 的段布局和符号约定，可以做针对性的链接优化
- **msh -> meow**：shell 可以直接调用 meow 的内部 API 而非走命令行

这种特化只影响 Kit 组件之间的内部协作路径，不影响对外标准兼容性。外部软件通过标准接口使用 Kit 工具，行为与 GNU 对应物一致。

### 6.2 参考资源（节省算力）

**核心原则：优先参考成熟社区实现，避免从零推导繁琐算法而浪费算力。**

#### 本仓库已提供的只读参考树（`reference/`，gitignored，勿改勿提交）

| 路径                        | 用途                                                                          |
| --------------------------- | ----------------------------------------------------------------------------- |
| `reference/cproc/`        | mcc 前端设计参考（词法/语法/语义/类型系统）                                   |
| `reference/qbe/`          | mcc 后端设计参考（IR/指令选择/寄存器分配/各 arch emit）                       |
| `reference/musl/`         | meuos-libc 算法参考（mallocng/stdio/pthread/...）                             |
| `reference/tinycc/`       | 轻量 C 编译器参考（快速编译、简单后端、tcc 的 preprocessor）                  |
| `reference/cxx-frontend/` | m++ C++ 前端参考（C++23 词法/语法/语义解析、AST 设计）                        |
| `reference/aburi/`        | m++ C++ 前端参考（lexer/parser/preprocessor/ast/constexpr、Itanium ABI 降级） |

#### 鼓励参考的其他社区资源

- **libc 算法**：musl（首选，已 vendored）、Cosmopolitan Libc、serenityOS LibC、PDCLib
- **编译器设计（C）**：cproc/QBE（已 vendored）、chibicc、9cc、lacc、cparser
- **编译器设计（C++）**：cxx-frontend（C++23 完整前端，已 vendored）、aburi（C/C++ 前端全流程，已 vendored）
- **构建系统**：redo、tup、ninja、bear-make
- **工具集**：Rust uutils、BusyBox、serenityOS Utilities
- **Shell**：dash（POSIX sh 参考）、serenityOS Shell
- **构建工具**：GNU m4/Bison/Flex/Gperf（参考行为和语法兼容性，不复制源码）
- **通用知识库**：OSDev Wiki、Linux man-pages、各 arch 的 ELF/ABI spec

#### 边界（与 §4 禁止事项一致）

- **参考算法与结构，但用本项目自己的代码重新实现**；不直接复制任何参考源码。
- 仍受 §4 约束：核心库与 mcc 源码中**禁止** glibc 专有符号、LLVM/Clang、GCC 代码。
- 所有实现必须能被 mcc 自身编译（自举验证），并过对应 `make check`。

**一句话**：站在巨人肩膀上--读参考实现理解算法，再用自己的手写出来，不要把算力花在重新发明轮子上。

---

## 7. 任务编排策略

> 本条是一般性方法论，适用于本项目所有复杂任务的规划与执行。
> 不只是移植工作，编译器特性实现、libc 函数实现、工具链开发等任何需要多步骤完成的任务，都应遵循此策略。
>
> **强制约定**：所有任务执行必须使用 Codebuddy 无头模式（hy3 模型，允许多轮对话），
> 取代普通 SubAgent。不允许使用默认的 SubAgent 模型执行实质性编码任务。
> hy3 无头 agent 可通过 Agent 工具 `subagent_type: "fork"` + `run_in_background: true`
> 或直接设置 `model: "hy3"` 模式发起。主 agent 使用 Agent 工具 spawn，子 agent 自动获得
> 多轮对话能力，无需手动续接上下文。

### 7.1 任务颗粒度原则

任务必须拆到足够细，使得**免费或低推理度模型也能独立完成**。

验收标准：

- 每个任务只做**一件事**：一个文件 / 一处修改 / 一个函数的实现
- 单任务上下文在 200 行以内，避免膨胀
- 禁止一个任务跨越多个不相关的文件或模块

反例（禁止）："实现 riscv64 的 7 个运行时文件"
正例："创建 `src/arch/riscv64/atomic.S`（380 行，25 个原子函数）"

### 7.2 任务卡片五要素

每个任务必须包含以下五项，缺一不可：

| 要素                  | 说明                                                | 示例                                                                                         |
| --------------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| **1. 任务 ID** | 单词-短横线-代号。机器可读、语义自明；不用纯数字或字母数字编号 | `bug-riscv64-emit`、`ld-shared`、`meow-template-subst` |
| **2. 任务范围** | 精确的文件路径和修改内容                            | 创建`src/arch/riscv64/atomic.S`，实现 `__atomic_*` 系列                                  |
| **3. 参考来源** | 本仓库已验证实现（最优先）+ 社区标准实现 + 规范文档 | `src/arch/aarch64/atomic.S`（本仓库模板）+ `musl arch/riscv64/atomic_arch.h`（社区参考） |
| **4. 验收标准** | 可写成 shell 单行断言的检查项                       | `riscv64-linux-gnu-gcc -c atomic.S` 通过 && `nm atomic.o                                   |
| **5. 依赖关系** | 仅依赖已完成的**前置**任务，线性单向无回溯    | 依赖 `riscv64-syscall` 已完成                                                              |

#### 任务 ID 命名规则

```
<组件>-<功能>-<修饰>     # 全部小写，短横线分隔
```

**组件前缀**（来源自明）：
- `bug-*`        — 阻塞性 bug（如 `bug-riscv64-emit`、`bug-i386-tls`）
- `mcc-*`        — 编译器相关
- `libc-*`       — C 库相关
- `ld-*`         — 链接器相关（mt/ld）
- `as-*`         — 汇编器相关（mt/as）
- `meow-*`       — 构建系统相关
- `arch-*`       — 跨架构适配
- `c23-*`        — C23 标准特性
- `target-*`     — 架构 Target/子架构
- `specs-*`      — 编译参数/配置
- `triple-*`     — 三元组/ABI 相关
- `ci-*`         — CI/CD/测试基础设施

**命名原则**：
- 一眼能看出是哪个组件 + 做什么
- 避免纯数字（agent 记不住 `task-42` 是什么意思）
- 有层次：`mcc-pic-verify`（mcc 的 PIC 验证）好于 `verify-pic`
- 文件名/函数名风格：`ld-shared`、`meow-wildcard`、`libc-math`

### 7.3 线性单向任务流

- 将任务 DAG **拉平成线性阶段**，每个阶段内无交叉依赖
- 阶段内互不依赖的独立任务可**并行分派**给 hy3 无头 agent（使用 Agent 工具 `run_in_background: true` + `model: "hy3"`）
- 每个阶段完成后**立即验证**，失败不回退、不被后续任务污染
- **禁止回溯**：不允许 `ld-so` 完成后发现 `ld-shared` 有问题再回去改

### 7.4 hy3 无头 agent 并行开发

识别同一阶段内互不依赖的并行任务窗口：

```
Phase A: riscv64-syscall（基础，必须串行先做）
  → riscv64-atomic, riscv64-setjmp, riscv64-sigreturn（互不依赖，可 3 个 hy3 无头 agent 并行）
  → riscv64-thread-clone（依赖前 3 个全部完成，串行收尾）
```

并行分派要点：

- 每个 hy3 无头 agent 卡片**自包含**（含参考路径 + 验收命令），不依赖外部上下文
- 使用 `Agent` 工具发起时设置 `model: "hy3"`、`run_in_background: true`，确保子 agent 使用正确的无头模式模型
- 并行任务完成后，**主 agent** 使用 `TaskOutput` 收集结果，统一验收和集成
- 并行数量 ≤ 4 个，避免上下文过大
- hy3 无头模式天然支持多轮对话，子 agent 可自主进行多步操作（读文件、修改、验证）无需主 agent 干预

### 7.5 参考来源收集（算力节约的核心）

**在开始编码前**，先用 Explore 子 agent（`subagent_type: "Explore"`）收集：

1. **本仓库已验证的同类实现**（最优先）：同项目其他架构的对应文件，直接对照转录
2. **社区标准实现**：musl 对应目录、Linux 内核 UAPI 头文件
3. **规范文档**：ELF ABI spec 对应章节、syscall 编号表、指令集手册

关键是制作**架构差异对照表**，一次性消除重复推导：

| 功能               | 参考架构 (aarch64) | 目标架构 (riscv64) | 目标架构 (loongarch64) |
| ------------------ | ------------------ | ------------------ | ---------------------- |
| syscall 指令       | svc#0              | ecall              | syscall 0              |
| syscall 号寄存器   | x8                 | a7                 | $a7                    |
| 线程指针           | tpidr_el0          | tp                 | $tp                    |
| 原子 load-reserved | ldaxr              | lr.w/lr.d          | ll.w/ll.d              |
| TLS 变体           | variant I (GAP=16) | variant I (GAP=0)  | variant I (GAP=0)      |

对照表一旦建立，所有 hy3 无头 agent 共享，避免各自重复查询。

### 7.6 阶段归档

每个阶段完成后**必须归档**，然后才能进入下一阶段。归档是提交的前置条件：

1. **运行 `make check`**（必须通过）。跨架构变更还需运行对应 `check-<arch>-bootstrap` 或 `check-<arch>-runtime`。如有回归**必须修复后才能提交**。
2. **更新 `.issues/` 文件**，将完成项标记为 `[x]`。
3. **更新 `ARCHITECTURE.md` / `PORTING.md`** 中的状态表和路线图。
4. **Git 提交**，提交信息格式：`<组件>: <阶段描述>（<文件清单>）`
   - 示例：`meuos-libc: riscv64 runtime 完成 (crt1/syscall/atomic/setjmp/sigreturn/thread_clone/tls)`
5. **合并到 `main`**（如果在工作分支上开发），合并后删除工作分支。
6. **禁止未通过 `make check` 就提交**，**禁止未归档就进入下一阶段**——归档是阶段完成的唯一定义。

### 7.7 完整执行模板

```
Phase 0: 环境确认（1 步，Explore 子 agent）
  → 验证参考文件存在、工具链就绪

Phase N: 主体实现（M 步，按依赖 DAG 串行+并行）
  → 基础文件（串行）
  → 独立文件（并行 hy3 无头 agent，使用 Agent tool model:"hy3" run_in_background:true）
  → 集成收尾（串行，TaskOutput 收集结果）
  → 每步验证 → 阶段归档

Phase N+1: 下一阶段（复用上一阶段的框架和对照表）
  → 适配差异项 → 验证 → 归档

Phase Final: 公共层清理 + 生成报告
```

---

### 7.8 循环任务模式

`cron.md` 定义基于 Cron 的循环任务（当前：`worktree-stable-enhance`，每 10 分钟一次）。
- 每个循环点启动独立 agent，不与前序任务共享上下文
- 多个 agent 并行编辑可能存在冲突风险
- 取消方式：`CronDelete("<作业 ID>")`，作业 ID 在 `cron.md` 中声明
- `cron.md` 随会话结束自动清理（session 级）

---

## 8. 构建与测试命令参考

> 本节提供操作该仓库时最常用的命令速查。所有组件均位于 `projects/<name>/` 下，
> 使用简单 Makefile 构建（§4 禁止 autotools/cmake/meson）。
> `make -C projects/<name> <target>` 是通用调用形式。

### 8.1 环境准备

```sh
# 设置 sysroot（必须）
export MEUOS_SYSROOT=/workspace/MeuOS-Kit/sysroot

# 检查宿主编译器
gcc --version || tcc --version

# QEMU VM 管理（env/ 目录下）
env/bin/qvm boot aarch64          # 启动 arm64 VM
env/bin/qvm run aarch64 '<cmd>'   # 在 VM 内执行命令
env/bin/qvm console aarch64       # 进入控制台
env/bin/qvm stop aarch64          # 停止 VM
```

### 8.2 组件构建

```sh
# mcc（默认宿主架构，6 个后端全部内置）
make -C projects/mcc                          # 构建 mcc 二进制
make -C projects/mcc HOST_CC=tcc              # 用 tcc 替代 gcc

# meuos-libc（默认 x86_64）
make -C projects/meuos-libc                   # 构建 x86_64 libc 核心
make -C projects/meuos-libc ARCH=aarch64      # 交叉编译 aarch64
make -C projects/meuos-libc ARCH=riscv64      # riscv64
make -C projects/meuos-libc ARCH=loongarch64  # LoongArch64
make -C projects/meuos-libc ARCH=i386         # i386
make -C projects/meuos-libc install DESTDIR=$PWD/sysroot PREFIX=/usr  # 安装到 sysroot

# meow
make -C projects/meow                         # 默认 mcc + sysroot
make -C projects/meow CC=cc                   # 使用宿主 cc（编译环境初始阶段）

# meuos-toolchain（一次性构建全部 9 个工具）
make -C projects/meuos-toolchain              # 构建 as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump

# meuos-sysroot（libmsys + mkmsys + msysctl）
make -C projects/meuos-sysroot                # 构建 libmsys.a + mkmsys + msysctl
make -C projects/meuos-sysroot so             # 构建 libmsys.so（Python 绑定用）

# mz - 压缩库
make -C projects/meuos-compress                           # 构建 libmz.a
make -C projects/meuos-compress check                     # 压缩/解压缩轮转测试

# meuos-buildtools（Phase 6）
make -C projects/meuos-buildtools             # 构建 m4/gperf/flex
```

### 8.3 测试

#### mcc 测试

```sh
# 基础门禁
make -C projects/mcc check                    # Phase 1a: hello world exit=0

# 标准回归
make -C projects/mcc check-c99                # C99 标准特性测试
make -C projects/mcc check-c11                # C11 全部（atomic/thread_local/varargs 等）
make -C projects/mcc check-c11-atomic         # C11 原子测试
make -C projects/mcc check-c23                # C23 测试（constexpr/embed/typeof）

# 后端回归（汇编级验证）
make -C projects/mcc check-targets            # 全后端目标汇编验证
make -C projects/mcc check-i386               # i386 后端回归
make -C projects/mcc check-loongarch64        # LoongArch64 后端回归

# 运行时回归（需要对应架构 sysroot/QEMU）
make -C projects/mcc check-i386-runtime       # i386 运行时（需 sysroot-i386）
make -C projects/mcc check-i386-qemu          # i386 QEMU VM 运行时
make -C projects/mcc check-aarch64-runtime    # aarch64 QEMU VM 运行时

# 驱动/集成
make -C projects/mcc check-driver             # 驱动测试（sysroot/feature）
make -C projects/mcc check-abi                # ABI 回归（bit-field aggregate）
make -C projects/mcc check-mt-integration     # mt 工具链集成（需已构建 meuos-toolchain）
make -C projects/mcc check-sysroot-static     # sysroot 内自重建验证

# 社区测试套件
make -C projects/mcc check-chibicc            # chibicc 社区测试
make -C projects/mcc check-community          # check-c99 + check-chibicc
```

#### meuos-libc 测试

```sh
# 宿主全套回归
make -C projects/meuos-libc check             # 编译+运行约 25 个测试程序

# 跨架构自检
make -C projects/meuos-libc check-aarch64-bootstrap      # aarch64 跨编译+可选 qemu 运行时
make -C projects/meuos-libc check-riscv64-bootstrap      # riscv64
make -C projects/meuos-libc check-loongarch64-bootstrap  # LoongArch64
make -C projects/meuos-libc check-i386-bootstrap         # i386

# 原生链接器验证
make -C projects/meuos-libc check-native-linker          # 通过 mt/ld 链接验证
make -C projects/meuos-libc check-mcc                    # 用 mcc 编译 libc 测试

# 全架构一步式
make -C projects/meuos-libc check-all                    # check + 全架构 bootstrap
```

跨架构运行时验证需要设置环境变量：

```sh
# aarch64 qemu-user 运行时
MEUOS_AARCH64_RUN=1 MEUOS_AARCH64_QEMU=env/qemu/qemu-aarch64-static \
  make -C projects/meuos-libc check-aarch64-bootstrap

# riscv64 qemu-user 运行时
MEUOS_RISCV64_RUN=1 MEUOS_RISCV64_QEMU=env/qemu/qemu-riscv64-static \
  make -C projects/meuos-libc check-riscv64-bootstrap
```

#### meow 测试

```sh
make -C projects/meow check                   # YAML 配方+Makefile 兼容+--bootstrap
make -C projects/meow check-sysroot-static    # sysroot 下自重建
```

#### meuos-toolchain 测试

```sh
make -C projects/meuos-toolchain check        # 全部 10+ 项测试
make -C projects/meuos-toolchain check-as-x86_64       # 汇编器基本测试
make -C projects/meuos-toolchain check-as-sse-x86_64   # SSE 汇编 golden bytes
make -C projects/meuos-toolchain check-ld-x86_64       # 链接器端到端
make -C projects/meuos-toolchain check-ar-bsd          # BSD 归档格式
make -C projects/meuos-toolchain check-libelf          # ELF 解析轮转
```

#### meuos-sysroot 测试

```sh
make -C projects/meuos-sysroot check         # 打包+校验+单元测试
make -C projects/meuos-sysroot msys          # 从 MEUOS_SYSROOT 生成 .msys
make -C projects/meuos-sysroot check-msys    # 检查已有 .msys 可读性
```

### 8.4 自举

```sh
./bootstrap.sh                                # Phase 0→1（默认）
./bootstrap.sh --phase 0                      # 仅 Phase 0
./bootstrap.sh --phase 2                      # Phase 0→2
./bootstrap.sh --phase 5                      # Phase 0→5 全流程
```

Phase 4 自举验证已通过（`check-sysroot-static`）：sysroot 内 mcc + meow 重新编译全套工具（82 个 .c + libmcc.a + mcc 链接），产物功能等价验证通过。

Phase 5 工具链完善已完成（mt/as + mt/ld 集成到 mcc driver，`check-mt-integration` 验证通过），Kit 全程零宿主依赖已验证。

### 8.5 跨架构构建须知

| 架构 | mcc 编译 C | 汇编器 | 系统依赖 |
|------|-----------|--------|---------|
| x86_64 | `$(HOST_CC)` | `$(HOST_CC)` | 无需交叉工具链 |
| i386 | `$(MCC) --target=i386` | `$(HOST_CC) -m32` | 需要 32-bit glibc 开发包 |
| aarch64 | `$(MCC) --target=aarch64` | `aarch64-linux-gnu-gcc` | 需要交叉 gcc |
| riscv64 | `$(MCC) --target=riscv64` | `riscv64-linux-gnu-gcc` | 需要交叉 gcc |
| loongarch64 | `$(MCC) --target=loongarch64` | `loongarch64-linux-gnu-gcc` | 需要交叉 gcc |

### 8.6 清理

```sh
make -C projects/mcc clean
make -C projects/meuos-libc clean
make -C projects/meow clean
make -C projects/meuos-toolchain clean
make -C projects/meuos-sysroot clean
```

### 8.7 测试调试指引

当 `make check` 或回归测试失败时，按以下路径排查：

**编译错误 → 常见原因：**
- **缺少 sysroot**：确认 `MEUOS_SYSROOT` 已设置，`$MEUOS_SYSROOT/usr/include` 存在
- **mcc 自身编译失败**：先用 `make -C projects/mcc && make -C projects/mcc check` 确认基础门禁通过
- **交叉工具链缺失**：检查对应架构的 gcc 交叉编译器是否存在（`aarch64-linux-gnu-gcc --version` 等）
- **引用未实现符号**：检查 `.issues/` 排查是否依赖了未实现的功能

**链接错误 → 常见原因：**
- **-l\<lib\> 顺序错误**：mcc 的链接器要求库按依赖顺序排列（引用者在被引用者之前）
- **MT_AS/MT_LD 集成问题**：用 `mcc -v` 查看实际调用的汇编/链接命令，确认走的是 mt 工具链
- **crt1.o 找不到**：确认 `$MEUOS_SYSROOT/usr/lib/crt1.o` 存在

**运行时崩溃 → 快速诊断：**
- **单步调试**：mcc 生成的可执行文件可用宿主 `gdb` 调试（静态链接，含 `-g` 调试信息）
- **qemu-user 运行时**：设置 `QEMU_LD_PREFIX=$MEUOS_SYSROOT` 避免动态库找不到
- **strace**：`strace -o /tmp/trace.log ./a.out` 定位系统调用级问题
- **回归比对**：用 gcc 编译相同源码，比对行为确认是 Kit 问题还是测试用例问题

**测试框架问题：**
- **跳过已知阻塞项**：某些测试依赖未实现特性（如 mcc i386 后端缺口会阻塞 libc i386 TLS 测试），检查 TODO 确认是否为已知阻塞
- **golden bytes 不匹配**：汇编测试（check-as-sse-x86_64 等）的 .expect 文件需要对应架构编码规则更新

---

## 9. 知识库管理（IMA 集成）

> 本项目集成了 IMA OpenAPI 技能（`ima-skill`），作为 MeuOS Kit 文档的统一外部知识库。
> 知识库中存储设计文档、会议记录、架构决策、移植笔记等不适合纳入代码仓库的内容。

### 9.1 分工边界

| 内容类型                              | 存放位置                 | 说明                                                                 |
| ------------------------------------- | ------------------------ | -------------------------------------------------------------------- |
| 功能需求、功能规格说明                 | IMA 知识库                | 功能需求的完整描述，包括使用场景、行为约束、验收条件                   |
| 设计方案、架构决策、技术选型           | IMA 知识库                | 设计讨论记录、备选方案对比、最终决策及其理由                           |
| 项目规划、路线图、阶段计划             | IMA 知识库                | Phase 级/里程碑级的规划文档，进度追踪和调整记录                        |
| 任务计划、实施步骤、分工安排           | IMA 知识库                | 具体功能的实现计划，包括任务拆解、依赖关系、验收标准                   |
| 会议记录、讨论结论                     | IMA 知识库                | 同步/异步讨论的完整记录和结论                                          |
| 组件规格、API 接口定义                 | 代码仓库 `ARCHITECTURE.md` | 随代码版本控制，代码变更时同步更新；IMA 中的设计决策落地后应更新此处    |
| 项目规约、任务编排策略、禁止事项       | `AGENTS.md`（本文件）     | Agent 初始化的核心参考                                                |
| 移植笔记、架构差异对照表               | IMA 知识库                | 多架构移植的实测经验记录                                               |
| 社区参考资源笔记                       | IMA 知识库                | 对 `reference/` 目录中社区源码的阅读笔记和分析                         |
| 构建/调试踩坑记录                      | IMA 知识库                | 现场调试的完整过程和解决方案                                           |
| 工具链就绪情况、编译器缺口的追踪       | IMA 知识库                | 跨架构测试的现场状态记录                                               |

**核心判据**：凡是**未来才实现**的需求/设计/规划 → IMA 知识库；凡是**已经实现**的规格和约束 → 代码仓库。IMA 中的设计决策落地后，应将最终确定的规格同步到 `ARCHITECTURE.md` 并标记知识库对应文档为「已实现」。

### 9.2 在 MeuOS Kit 中使用 ima-skill

`ima-skill` 提供两类操作：**笔记管理（notes）** 和 **知识库操作（knowledge-base）**。

**常用场景与对应操作：**

| 场景                               | skill 模块         | 关键步骤                                                                 |
| ---------------------------------- | ------------------ | ------------------------------------------------------------------------ |
| 搜索知识库中 MeuOS 相关文档           | knowledge-base     | `search_knowledge` → 指定 `query` 关键词                                 |
| 查看某篇知识的原始内容               | knowledge-base     | `get_media_info` → 获取 `media_id` → 下载原文                             |
| 浏览知识库内容列表                   | knowledge-base     | 先 `search_knowledge_base` 获取知识库 ID → 再 `get_knowledge_list`       |
| 新建一篇设计笔记                     | notes              | `import_doc` → 指定 `content`（Markdown 格式）和 `title`                  |
| 追加调试记录到已有笔记               | notes              | `search_note` 找到笔记 → `append_doc`                                     |
| 上传架构差异对照表文件到知识库       | knowledge-base     | `preflight-check` → `create_media` → COS Upload → `add_knowledge`        |

### 9.3 配置要求

使用 `ima-skill` 需要配置 IMA OpenAPI 凭证：

```bash
# 方式 A：配置文件（推荐）
mkdir -p ~/.config/ima
echo "your_client_id" > ~/.config/ima/client_id
echo "your_api_key" > ~/.config/ima/api_key

# 方式 B：环境变量
export IMA_OPENAPI_CLIENTID="your_client_id"
export IMA_OPENAPI_APIKEY="your_api_key"
```

凭证优先级：环境变量 → 配置文件。缺少凭证时 API 调用以 code `-100` 退出。

### 9.4 Agent 启动时主动读取规划文档

**这是项目的第一规约：任何 agent 会话启动后，必须主动读取 IMA 知识库中的规划文档。**

规划文档是"接下来做什么"的权威来源，优先级高于代码仓库中的任何 .issues 文件。
规划设计可能先于代码存在，只有主动读取才能理解当前阶段的目标。

#### 执行步骤（在 §0 会话恢复流程的第 1 步执行）

```sh
# 1. 找到 MeuOS 知识库
#    使用 ima-skill 的 knowledge-base 模块：
#    search_knowledge_base(query: "MeuOS")

# 2. 浏览知识库内容，查找规划类文档
#    get_knowledge_list(knowledge_base_id="<上一步返回的 kb_id>")
#    重点查找标题包含以下关键词的文档：
#    - "规划" / "计划" / "路线图" / "路线"
#    - "需求" / "功能规格" / "设计"
#    - "阶段" / "Phase" / "P0" / "P1" / ...
#    - "TODO" / "待办" / "任务"
#    - "v4.0" / "v4"（最新的版本号）

# 3. 阅读每个规划文档的原始内容
#    get_media_info(media_id="<文档的 media_id>")
#    下载并阅读全文，理解：
#    - 当前阶段的目标是什么
#    - 有哪些待实现的功能/架构
#    - 设计方案和验收条件
#    - 与其他组件的依赖关系

# 4. 将规划内容与 AGENTS.md §10（项目状态速查）交叉对比
#    - 规划中提到的待办项是否已在仓库 .issues/ 中记录
#    - 规划中的设计决策是否需要更新 ARCHITECTURE.md
```

#### 查询模板（可直接执行）

```bash
source ~/.bashrc
cd /workspace/MeuOS-Kit/.codebuddy/skills/ima-skill
SKILL_DIR="$(pwd)"
OPTS=$(printf '{"clientId":"%s","apiKey":"%s"}' "$IMA_OPENAPI_CLIENTID" "$IMA_OPENAPI_APIKEY")

# 搜索 MeuOS 知识库
resp=$(node "$SKILL_DIR/ima_api.cjs" "openapi/wiki/v1/search_knowledge_base" \
  '{"query":"MeuOS","cursor":"","limit":5}' "$OPTS" 2>/dev/null)
KB_ID=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['info_list'][0]['kb_id'])" 2>/dev/null)

# 浏览知识库内容
node "$SKILL_DIR/ima_api.cjs" "openapi/wiki/v1/get_knowledge_list" \
  "{\"knowledge_base_id\":\"$KB_ID\",\"cursor\":\"\",\"limit\":50}" "$OPTS" 2>/dev/null | \
  python3 -c "
import sys, json
data = json.load(sys.stdin)['data']['info_list']
for item in data:
    title = item['title']
    tags = ' '.join(['📋' if kw in title else '' for kw in ['规划','计划','路线','需求','设计','TODO','v4']])
    print(f\"  {tags} {title}\")
"
```

#### 阅读后的行动

读取规划文档后，agent 应：

1. **更新对当前阶段的理解**：规划文档中描述的目标是什么，当前进展到哪里
2. **确认 `.issues/` 的同步状态**：规划中提到的待办是否已在 `.issues/` 中有对应条目
3. **确定本次会话的工作范围**：从规划中选取一个具体的、可独立完成的任务
4. **如有模糊之处**：在 IMA 知识库搜索相关设计笔记补充上下文，或向用户确认

### 9.5 文档贡献指南

向知识库贡献 MeuOS 文档时遵循以下原则：

1. **标题格式**：`MeuOS/<主题>` — 例如 `MeuOS/mcc-i386-缺口分析`
2. **内容格式**：Markdown，保持简洁的技术笔记风格
3. **分类**：按阶段/组件组织，便于搜索
4. **关联代码**：提及代码文件时注明相对路径（如 `projects/mcc/src/driver/main.c`）
5. **定期清理**：过时文档标记为「已归档」或在笔记标题中添加 `[存档]` 前缀
6. **与仓库同步**：当某个设计决策最终被编码实现后，在知识库中标记对应记录为「已实现」

### 9.6 Codebuddy 技能清单

本项目配置了以下 Codebuddy 技能（位于 `.codebuddy/skills/`，软链接到 `.agents/skills/`）：

| 技能 | 用途 |
|------|------|
| `cross-test` | 跨架构测试编排 |
| `ima-skill` | IMA 知识库/笔记管理（§9.1-9.5） |
| `mkit-bootstrap` | Phase 0-5 自举流程编排（调用 bootstrap.sh） |
| `mkit-c11-check` | mcc C11 符合性检查（`_Atomic`/`_Generic`/`_Thread_local` 等） |
| `mkit-doc-sync` | 代码变更后文档同步收尾，确保文档不落后于代码 |
| `mkit-syscall-gen` | 生成单文件 syscall 封装（meuos-libc syscall 目录） |

技能通过 `Skill` 工具或对应的 slash command 调用。

---

## 10. 项目状态速查

### 10.1 已完成里程碑

- **mcc C11 完整实现** — `_Atomic`/`_Generic`/`_Thread_local`/`_Alignas`/`_Alignof`/`_Noreturn`/`_Static_assert`/匿名结构体/复合字面量/变长数组
- **mcc C23 特性** — `constexpr`/`typeof`/`typeof_unqual`/`nullptr_t`/`#embed`/`__has_include`/`[[]]` 属性/`#elifdef`/`#elifndef`/`#warning`/二进制字面量/数字分隔符/空初始化器/`auto` 类型推导/Labeled break/continue/`bool`/`true`/`false`/`_BitInt(N)`/`_Decimal32`/`64`/`128`/`static_assert` 无消息
- **6 个后端全部内置** — x86_64 / aarch64 / riscv64 / i386 / loongarch64 / arm（新增）
- **arm 完整移植（2026-07-27）** — mcc 后端 + libc 运行时（9 文件）+ mt as+ld，qemu-arm 验证通过
- **meuos-libc x86_64 完整运行验证** — stdio/stdlib/string/thread/signal/syscall/compat 全覆盖
- **meuos-libc aarch64 qemu 端到端验证通过**
- **meow 构建系统** — YAML 配方 / Makefile 兼容 / 并行构建（`-jN`）/ DAG 增量构建
- **meuos-toolchain 9 工具** — as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump
- **Phase 4 自举验证通过** — sysroot 内 mcc + meow 自重建全套工具
- **Phase 5 零宿主依赖验证通过** — mcc driver 集成 `MT_AS`/`MT_LD`，`check-mt-integration` 通过
- **.msys v2 完整实现** — v2 格式（64B header + 32B index + dir block）、SHA-256 去重/校验、ed25519 签名、Overlay 分层、流式消费、扩展块机制、xattr 扩展属性、msysctl 统一 CLI（22+ 命令）、Python ctypes 绑定。`msysctl fzf` 交互式浏览器支持。
- **稳定增强 worktree 完成（2026-07-29）** — 64 次提交全面完善全套工具链：
  - **meow `.meow` 自定义格式** — 替代 YAML，`run:` shell 脚本块 + `%VAR%` 插值 + 三元组推断 + `meow init/show/lint` 子命令
  - **ld TLS 动态模型完整实现** — link.c TLSGD/TLSLD/DTPMOD/DTPOFF 重定位 + ld.so PT_TLS/模块ID/连续布局/`__tls_get_addr` + bug-mt-so-undef 修复（shared UNDEF → JUMP_SLOT）
  - **mt-info 统一 ELF 分析工具** — 7 子命令（info/headers/deps/strings/which/diff/inspect）+ `--json` 跨工具输出
  - **mcc `--warn=` 语义警告体系** + 彩色诊断 + `--error-json` + triple 统一解析
  - **march-generic / cpu_detect** — `-march=native`/`x86-64-vN` + `/proc/cpuinfo` 跨架构回退
  - **as-isa-gating** — 两层指令门控（VEX 前缀快速检查 + required_features 精确门控）+ `--march=x86-64-vN`
  - **riscv-extensions / arm-multiver / i386-variants / aarch64-ext** — 全架构 `-march` 解析 + 特性位映射
  - **ci-pipeline / community-tests** — GitHub Actions 工作流 + chibicc 测试套件修复
- **`.meow` 宏系统完整实现（2026-07-29）** — 25+ 语义宏：
  - **run 修饰符** — `run(!)` 遇错中断 / `run(?)` 遇错继续 / `run(q)` 安静执行
  - **语义节** — `env:` 环境变量 / `download:` 下载 / `has:` 工具检测 / `lib:` 库检测 / `log:` 构建日志
  - **构建参数** — `toolchain:` 交叉前缀 / `cflags:` / `ldflags:` / `srcdir:` / `builddir:` / `parallel:` 并行度
  - **构建流水线** — `sha256:` 校验 / `unpack:` 解包 / `patch:` 补丁 / `test:` 测试 / `copy:` 复制 / `strip:` 去调试符号
  - **流程控制** — `only:` / `except:` 架构过滤 / `workdir:` 工作目录 / `pre:` / `post:` 钩子 / `error:` 回调 / `meta:` 元数据
- **pkgs/* 全面迁移至 `project.meow`** — 18 个构建包全部使用 `.meow` 配方格式，兼容保留 `meow.yaml`
- **pkg-config 内置替代** — `meow pkg-config` 子命令 + 27 库内置参数表 + recipe `uses:` 字段集成 + 构建时 `%LIBS%`/`%CFLAGS%` 自动展开
- **netdb.h 完整 POSIX 实现** — host/serv/proto/net 解析 + getaddrinfo/getnameinfo + reentrant `_r` 变体 + 7 项回归测试

### 10.2 待启动/进行中工作

| 工作项                                 | 状态     | 备注                                                             |
| -------------------------------------- | -------- | ---------------------------------------------------------------- |
| m++ C++ 前端（阶段 B/C/D）              | ⏳ 待启动 | 阶段 A（libmcc 分离）已完成；子阶段待 m++ 启动时实施               |
| meuos-buildtools（m4/bison/flex/gperf） | ⏳ 待启动 | Phase 6；替换 GNU 构建工具依赖                                    |
| meuos-utils（coreutils 等）             | ⏳ 待启动 | Phase 7；coreutils/diffutils/findutils 替代                       |
| meuos-shell (msh)                       | ⏳ 待启动 | Phase 7；POSIX sh + 可选 bash/zsh 兼容                            |
| meow `meowctl` 配置界面                | ⏳ 待设计 | `.meow` 格式已可用，配置/查询 CLI 待设计                           |
| meow 原生 shell 替代                   | 🔄 进行中 | 用 msh 替代 /bin/sh（阻塞于 msh 可用性）                          |
| mt DWARF 调试信息（P8）                 | ⏳ 待启动 | 调试信息生成                                                       |
| arm-multiver emit 多版本分支            | 🟡 待补 | ARM emit 层 `g_arm_arch_ver` 已就绪，v6/v7+/v8 指令分支待落地        |

### 10.3 各架构支持矩阵

| 架构         | mcc 后端 | libc 核心 | mt/as     | mt/ld     | qemu 运行时验证       | 系统依赖                   |
| ------------- | -------- | --------- | --------- | --------- | --------------------- | -------------------------- |
| x86_64        | ✅       | ✅        | ✅ P0-P9  | ✅ P0-P9  | ✅ 完整验证           | 无                         |
| aarch64       | ✅       | ✅        | ✅        | ✅        | ✅ qemu 端到端        | `aarch64-linux-gnu-gcc`    |
| riscv64       | ✅       | ✅        | ✅ P11    | ✅ P11    | 🟡 exit=42 通过       | `riscv64-linux-gnu-gcc`    |
| i386          | ✅       | ✅        | ✅        | ✅        | 🟡 qemu 系统仿真      | `gcc -m32` + 32-bit libc   |
| loongarch64   | ✅       | ✅        | ✅        | ✅        | 🟡 exit=42 通过       | `loongarch64-linux-gnu-gcc` |
| arm           | ✅       | ✅        | ✅        | ✅        | ✅ qemu-arm 运行时     | `arm-linux-gnueabihf-gcc`  |

### 10.4 相关文档索引

| 文档路径                                           | 内容                                    |
| -------------------------------------------------- | --------------------------------------- |
| `AGENTS.md`（本文件）                                | 项目规约、命令参考、知识库管理           |
| `README.md`                                        | 快速开始与项目简介                      |
| `projects/mcc/ARCHITECTURE.md`                     | 编译器架构、模块职责、阶段状态           |
| `projects/meuos-libc/ARCHITECTURE.md`              | C 库目录结构与模块职责                  |
| `projects/meuos-libc/PORTING.md`                   | 多架构移植说明、ABI 契约、time64 策略   |
| `projects/meow/ARCHITECTURE.md`                    | 构建系统模块职责与数据流                |
| `projects/meuos-toolchain/ARCHITECTURE.md`         | 工具链架构、P0-P11 分阶段任务           |
| `projects/meuos-sysroot/ARCHITECTURE.md`           | .msys 格式设计与依赖关系               |
| `env/README.md`                                    | QEMU 测试环境使用说明                  |
| `.issues/`                                        | 待办任务跟踪（日期编号，如 0728.md）   |
| IMA 知识库（通过 `ima-skill` 访问）                 | 设计笔记、移植记录、调试踩坑           |
| `.github/workflows/ci.yml`                            | CI 管道定义                            |

### 10.5 CI 管道

`.github/workflows/ci.yml` 定义 GitHub Actions 工作流，push/PR 到 `main` 和 `worktree-*` 分支时触发：

| 步骤 | 说明 |
|------|------|
| 安装依赖 | build-essential + qemu-user |
| 构建 meuos-sysroot | libmsys.a + mkmsys + msysctl |
| 构建 meuos-toolchain | 9 个工具的完整构建 |
| 工具链回归测试 | `make -C projects/meuos-toolchain check` |
| 构建 mcc | C99+C11+C23 编译器 |
| mcc 回归测试 | check + check-c99 + check-c11 + check-c23 |
| chibicc 社区测试 | check-chibicc（社区功能测试套件） |
| 构建 meow | 使用宿主 cc（CI 环境初始阶段） |
| 跨架构运行时 | riscv64 / aarch64 / i386 qemu-user（条件性） |
| 多目标汇编测试 | `meuos-toolchain` 多架构汇编验证 |
| 失败处理 | 自动上传测试日志到 artifacts |

---

## 11. Issue/TODO 导航系统

> 当前项目 issue/todo 分布在多个位置，各司其职。本节是统一的导航入口，
> 帮助 agent 快速定位到正确的追踪文件。

### 11.1 职责分工

| 层级 | 位置 | 内容 | 何时更新 |
|------|------|------|----------|
| **全局-高层** | `AGENTS.md §10` | 各组件总体完成状态、里程碑、支持矩阵 | 阶段归档/里程碑完成时 |
| **全局-详细** | `issue/` 目录（如 `issue/0729.md`） | 跨组件详细待实现清单、缺陷分析、代码审查报告 | 审查/分析完成后追加 |
| **子项目-结构** | `projects/<name>/ARCHITECTURE.md` | 目录结构、模块职责、路线图、阶段状态 | 实现新功能/架构变更时同步 |
| **子项目-待办** | `projects/<name>/.todo/` | 每个 `.todo` 文件一个主题的详细设计、实现计划和验收条件 | 该主题开始工作时创建，完成后归档 |
| **子项目-移植** | `projects/<name>/PORTING.md` | 多架构移植说明、ABI 契约、特定架构边界 | 跨架构变更时同步 |
| **工作树-队列** | `.issues/INDEX.md` | worktree 中按优先级分组的任务队列（P0→P7 和设计原则） | worktree 活跃期间持续更新 |
| **工作树-入口** | `.issues/AGENT.md` | worktree 特定代理上下文、工作纪律、执行策略 | worktree 创建/变更时更新 |
| **循环任务** | `cron.md` | Chron 循环作业定义（当前 `worktree-stable-enhance` 每 10 分钟） | 添加/取消 cron 时更新 |
| **组件规格** | IMA 知识库 | 设计文档、会议记录、架构决策、移植笔记 | 设计讨论后立即追加，落地后同步到代码仓库 |

### 11.2 信息流与优先级

```
IMA 知识库 (规划/设计/需求)
    │  Agent 启动时主动读取 (§9.4)
    ▼
AGENTS.md §10 (全局完成状态速查)
    │
    ├──→ projects/<name>/ARCHITECTURE.md (组件结构 + 路线图)
    │         │
    │         └──→ projects/<name>/.todo/ (子任务详细设计 + 验收)
    │
    ├──→ issue/ (跨组件待实现 + 代码审查结果)
    │
    └──→ .issues/INDEX.md + .issues/AGENT.md (worktree 任务队列)
              │
              └──→ cron.md (循环执行)
```

**读取优先级**（从高到低）：
1. **IMA 知识库规划文档** — 权威来源，Agent 启动后必须优先读取（§9.4）
2. `.issues/AGENT.md` + `.issues/INDEX.md` — 当前 worktree 上下文（仅 worktree 中有效）
3. `AGENTS.md §10` — 全局状态速查
4. `issue/` — 详细待实现/缺陷清单
5. `projects/<name>/ARCHITECTURE.md` + `.todo/` — 组件级详细计划

### 11.3 Issue 文件命名约定

`issue/` 目录下的文件按日期命名：

- 文件名格式：`<MMDD>.md`（如 `0729.md` 表示 7 月 29 日创建/更新）
- 内容包含：验证日期、逐项确认状态、汇总优先级
- 过期文件：标记 `[存档]` 前缀或移入 `issue/archive/`

### 11.4 .todo 文件生命周期

1. **创建** — 当某个主题（新架构移植、新功能、重构）需要详细计划时，在对应组件下创建 `.todo/<topic>.md`
2. **内容** — 任务范围、参考来源、验收标准、依赖关系（参考 §7.2 任务卡片五要素）
3. **归档** — 实现完成后，`.todo` 文件内容合并到 `ARCHITECTURE.md` 或 `PORTING.md`，`.todo` 文件标记 `[x]` 或删除

>>>>>>> Stashed changes
