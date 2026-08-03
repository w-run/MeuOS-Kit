# 组件规范参考（.agents/reference/components.md）

> 从 AGENTS.md §2 下放（2026-08-04）。组件详细规范，按需读取。
> 状态权威来源：各组件 。

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

**C++ 目标（m++）**：m++ 复用 mcc 的后端（IR/指令选择/寄存器分配/汇编输出），通过 `libmcc` 共享后端库实现。m++ 前端独立实现 C++ 语法/语义。**当前状态**：C++98~23 主路线图已完成（含 C++23 四缺口、依赖 NTTP、concept、if constexpr、constexpr 类对象等，详见 `.agents/knowledge/project_mcc_cpp.md`）。

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


---

## 2.8 软件包策略：Kit 实现 vs meow 构建

参考 LFS 12.1 软件包列表，分为两类：

### Kit 实现（自举链基础设施）

Kit 组件是构建其他软件的基础设施，必须由 Kit 自己实现，不依赖 GNU 对应物：

| LFS 软件包                    | Kit 组件                                   | 状态                   |
| ----------------------------- | ------------------------------------------ | ---------------------- |
| Glibc                         | meuos-libc                                 | ✅                     |
| GCC                           | mcc/m++                                    | ✅ C11，✅ C23，✅ C++ 主路线图 |
| Binutils                      | meuos-toolchain                            | ✅ P0-P4（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump + mt 集成） |
| Make                          | meow                                       | ✅                     |
| M4/Bison/Flex/Gperf           | meuos-buildtools                           | 待启动                 |
| Coreutils/Diffutils/Findutils | meuos-utils                                | 🟡 骨架（P1 烟雾 5 工具）|
| Gawk/Sed/Grep                 | meuos-utils（文本处理）                    | ⏳ 待启动                |
| Bash                          | meuos-shell (msh)                          | 🟡 骨架（-c + script + REPL）|
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


---

## 2.9 mz - 压缩库

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

### 2.10 meuos-libtui - 终端 UI 库

**定位**：为 MeuOS Kit 组件提供纯 C11 终端 UI 支持，零第三方依赖（仅 POSIX termios + ANSI/XTerm 转义序列）。

**分层 API**：底层原子操作 → 可组合布局树 → 显示模式模板 → 交互式组件（全屏/弹窗/向导/分栏/对话框/列表/输入框）。

**设计目标**：轻量（不依赖 ncurses/readline/terminfo）、零依赖（libtui.a 单静态库）、可组合（组件基于 `tui_render_fn` 回调）、主题化（MeuOS 绿色调色板）。

**适用场景**：grub、shell、packagemanager、build、download、config、setup、install、AI agent、editor、reader、debug tool、remote connect 等全部 MeuOS 场景。

**当前能力**：原始模式、光标定位、颜色/样式、屏幕清除等底层 API 已完成（P0-P2），文本缓冲/行编辑/24-bit 真彩色/辅助 UI 组件（P3+）待实现。

**How to apply**: 其他组件需要 TUI 功能时，链接 `libtui.a` + `#include <meuos/libtui.h>`。详见 `projects/meuos-libtui/ARCHITECTURE.md`。

---

### 2.11 meuos-kernel - MeuOS 内核

**定位**：MeuOS 自研内核（设计/规划阶段）。承载于 `kernel-plan` 工作树（分支 `worktree-kernel-plan`）。

**内核定位**（来自 IMA 知识库「MeuOS / 01-总体架构」）：
- 外部兼容 Linux ABI（经兼容层对接，已有生态二进制可运行），内部自主设计——Linux 是参照物，不是目标。
- 三大内在统一设计方向：① 统一文件系统层（内置校验和/压缩/加密 + 原子 `.msys` sysroot 更新 + 统一资源命名空间）；② spawn 模型进程管理（内建监督、资源账本 Day 1、简化信号）；③ capability-based 安全（Day 1 唯一模型、pledge/unveil 沙箱、内核关键组件用 Rust、最小 TCB）。
- IPC 参考：同步端点取 seL4/Zircon，事件通知取 kqueue，服务间通信取 Binder/XPC。

**现状与路线**：Kit 层（工具链/5 架构）已完成；内核处于「规划中」，路线：技术选型 → 原型（内存+调度+IPC+Linux syscall 兼容层 mkit 子集）→ 验证（mcc+meuos-libc 在自研内核自举）。

**文档**：`projects/meuos-kernel/` 下 80+ 设计文档（00-总览与路线图、01-架构选型、02-语言策略、03-内存管理... 82-第十轮收敛摘要），由多 agent 协同调研后聚合生成。

---
