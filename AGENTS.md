# MeuOS Kit - Agent 初始化 Prompt

> Update: 2026-07-25
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
> - 仅在以下情况可提交到 `main`：一次性修复（如 typo、编译报错修正）、文档同步、`.todo` 状态更新、纯重构不涉及功能变更。
> - 每次提交前必须跑对应组件的 `make check`，确保不引入回归。
> - 提交信息格式：`<组件>: <描述>`，例如 `mcc: fix va_list alignment on i386`。
>
> **会话恢复**：因不可抗力会话可能中断。新会话先读对应子项目的 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项），再按需读本文件（项目规约）。各子项目独立维护状态，无全局 STATE 文件。

**项目名称**：MeuOS Kit
**项目定位**：MeuOS Next 的完整自举开发工具集。提供从零自举所需的全部工具：C/C++ 编译器、标准 C 库、构建系统、底层工具链、核心工具集与 Shell。
**许可**：RFL (Run Free Software License) v1.0

**核心组件**：

- `meuos-libc` - 标准 C 库（ISO C11 + POSIX，零 GNU 依赖；compat 层独立归档）
- `mcc` / `m++` - 编译器（C99+C11 完整实现 + C23 稳定实现；后续 C++ 共享后端）
- `meow` - 构建系统（取代 make + autoconf，MeuOS 中不依赖 make）
- `meuos-toolchain` - 底层工具链（as/ld/ar/ranlib，完全取缔 binutils）
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

**路线图**：P0-P2（x86_64 静态）已完成，P3-P11 详见 `projects/meuos-toolchain/ARCHITECTURE.md`。

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
| Binutils                      | meuos-toolchain                            | ✅ P0-P2               |
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

## 3. 自举流程

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
├── projects/
│   ├── mcc/                C/C++ 编译器（C99+C11+C23；m++ 待启动）
│   ├── meuos-libc/         标准 C 库（ISO C11 + POSIX；含 compat 兼容层）
│   ├── meow/               构建系统（取代 make + autoconf）
│   ├── meuos-toolchain/    底层工具链（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump）
│   ├── meuos-utils/        核心工具集（待启动）
│   ├── meuos-shell/        Shell 终端（待启动）
│   ├── pkgs -> ../pkgs     meow 构建配方软链接
│   └── sysroot{-<arch>}/   安装目标根文件系统（多架构）
├── env/                    QEMU 多架构测试环境（6.6.142 内核 + 9p 共享）
│   ├── bin/qvm             VM 管理器
│   ├── qemu/               静态 qemu-user 二进制（aarch64/riscv64/loongarch64）
│   ├── kernels/<arch>/     Alpine linux-virt 6.6.142 内核
│   └── rootfs/             initramfs 镜像
├── pkgs/                   meow 构建配方（YAML；dash/bzip2/binutils 等）
├── sysroot/                安装目标根文件系统（默认 MEUOS_SYSROOT）
└── reference/              cproc/QBE/musl 只读参考源（gitignored）
```

每个组件目录含 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项）。

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

### 7.2 任务卡片四要素

每个任务必须包含以下四项，缺一不可：

| 要素                  | 说明                                                | 示例                                                                                         |
| --------------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| **1. 任务范围** | 精确的文件路径和修改内容                            | 创建`src/arch/riscv64/atomic.S`，实现 `__atomic_*` 系列                                  |
| **2. 参考来源** | 本仓库已验证实现（最优先）+ 社区标准实现 + 规范文档 | `src/arch/aarch64/atomic.S`（本仓库模板）+ `musl arch/riscv64/atomic_arch.h`（社区参考） |
| **3. 验收标准** | 可写成 shell 单行断言的检查项                       | `riscv64-linux-gnu-gcc -c atomic.S` 通过 && `nm atomic.o                                   |
| **4. 依赖关系** | 仅依赖已完成的**前置**任务，线性单向无回溯    | 依赖 task-01（syscall.S）已完成                                                              |

### 7.3 线性单向任务流

- 将任务 DAG **拉平成线性阶段**，每个阶段内无交叉依赖
- 阶段内互不依赖的独立任务可**并行分派**给 hy3 无头 agent（使用 Agent 工具 `run_in_background: true` + `model: "hy3"`）
- 每个阶段完成后**立即验证**，失败不回退、不被后续任务污染
- **禁止回溯**：不允许 task-N 完成后发现 task-M（M < N）有问题再回去改

### 7.4 hy3 无头 agent 并行开发

识别同一阶段内互不依赖的并行任务窗口：

```
Phase A: task-01（基础，必须串行先做）
  → task-02, task-03, task-04（互不依赖，可 3 个 hy3 无头 agent 并行）
  → task-05（依赖 02-04 全部完成，串行收尾）
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
2. **更新 `.todo` 文件**，将完成项标记为 `[x]`。
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
# mcc（默认宿主架构，5 个后端全部内置）
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

### 8.4 自举

```sh
./bootstrap.sh                                # Phase 0→1（默认）
./bootstrap.sh --phase 0                      # 仅 Phase 0
./bootstrap.sh --phase 2                      # Phase 0→2
./bootstrap.sh --phase 5                      # Phase 0→5 全流程
```

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
```
