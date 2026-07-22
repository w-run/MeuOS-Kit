# MeuOS Kit - Agent 初始化 Prompt

> Update: 2026-07-22
>
> IMPORTANT: 全程思考/回复/文档优先使用简体中文
>
> **会话恢复**：因不可抗力会话可能中断。新会话先读对应子项目的 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项），再按需读本文件（项目规约）。各子项目独立维护状态，无全局 STATE 文件。

**项目名称**：MeuOS Kit
**项目定位**：MeuOS Next 的完整自举开发工具集。提供从零自举所需的全部工具：C/C++ 编译器、标准 C 库、构建系统、底层工具链、核心工具集与 Shell。
**许可**：MIT

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
- **C23**：稳定实现（`constexpr`、`typeof`、`nullptr`、`#embed`、属性语法等）

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

| 工具 | 功能 | 取代 |
|------|------|------|
| `as` | 汇编器（.s -> .o） | gas |
| `ld` | 链接器（.o/.a -> ELF） | GNU ld |
| `ar` | 归档器（.o[] -> .a） | GNU ar |
| `ranlib` | 归档索引生成 | GNU ranlib |
| `nm` | 符号列表 | GNU nm |
| `objdump` | 反汇编和节区查看 | GNU objdump |
| `readelf` | ELF 结构查看 | GNU readelf |
| `strip` | 删除调试/非必要符号 | GNU strip |
| `objcopy` | 节区和格式复制 | GNU objcopy |

**设计原则**：不包含宿主 `<elf.h>`，所有 ELF 常量自带。代码零 GNU/binutils 依赖。内部共享 `libelf` 库。

**路线图**：P0-P2（x86_64 静态）已完成，P3-P11 详见 `projects/meuos-toolchain/ARCHITECTURE.md`。

---

### 2.5 meuos-utils - 核心工具集

**目标**：提供 coreutils/diffutils/findutils 的完整替代，兼容 GNU 系列工具套件。参考 Rust 的 uutils 项目设计。

**工具范围**：

| 类别 | 工具 |
|------|------|
| coreutils | `ls`/`cp`/`mv`/`rm`/`cat`/`echo`/`mkdir`/`rmdir`/`touch`/`ln`/`chmod`/`chown`/`wc`/`head`/`tail`/`sort`/`uniq`/`cut`/`tr`/`tee`/`dd`/`df`/`du` 等 |
| diffutils | `diff`/`cmp`/`patch` |
| findutils | `find`/`locate`/`xargs` |
| 其他 | `grep`/`sed`/`awk`（文本处理三件套） |

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

| 工具 | 功能 | 取代 | 典型依赖场景 |
|------|------|------|-------------|
| `m4` | 宏处理器 | GNU m4 | autoconf 的 configure 生成、 Bison 输出后处理 |
| `bison` | 解析器生成器（.y -> .c/.h） | GNU Bison | binutils, bash, gawk, flex 等的语法分析 |
| `flex` | 词法分析器生成器（.l -> .c） | Flex | binutils, bash, gawk, wc 等的词法扫描 |
| `gperf` | 完美哈希函数生成器 | GNU gperf | glib, libidn2 等的关键字查找 |

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

| LFS 软件包 | Kit 组件 | 状态 |
|-----------|---------|------|
| Glibc | meuos-libc | ✅ |
| GCC | mcc/m++ | ✅ C11，C23/m++ 待 |
| Binutils | meuos-toolchain | ✅ P0-P2 |
| Make | meow | ✅ |
| M4/Bison/Flex/Gperf | meuos-buildtools | 待启动 |
| Coreutils/Diffutils/Findutils | meuos-utils | 待启动 |
| Gawk/Sed/Grep | meuos-utils（文本处理） | 待启动 |
| Bash | meuos-shell (msh) | 待启动 |
| Autoconf/Automake/Libtool | 被 meow 取代，不实现 | — |
| CMake/Ninja | 被 meow 取代，不实现 | — |

### meow 软件包（从源码构建，不自己实现）

这些是应用级库和软件，用 Kit 工具从源码编译安装，通过 `meow build <package>`：

| 类别 | 软件包 | 说明 |
|------|--------|------|
| 数学库 | GMP, MPFR, MPC | GCC 的依赖，mcc 不需要；Python/加密库等需要时通过 meow 构建 |
| 数据库 | GDBM, Berkeley DB | 应用级存储库 |
| 压缩 | Zlib, Bzip2, Xz, Zstd | 基础依赖库 |
| 终端 | Ncurses, Readline | msh 可自实现行编辑；其他软件需要时通过 meow 构建 |
| 正则 | Pcre2 | grep 可用 POSIX 正则；需要 PCRE 的软件通过 meow 构建 |
| 加密 | OpenSSL/LibreSSL, GnuPG | 安全库 |
| 解释器 | Python, Perl | 脚本语言 |
| 系统工具 | Util-linux, Procps, E2fsprogs | 系统管理工具 |
| init | — | MeuOS Next 自有 init，不用 systemd/sysvinit |

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

- 根目录有 `README.md`（项目说明 + 构建方法）。
- 每个组件可独立编译（但共享公共头文件和构建逻辑）。
- 每个组件有 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项）。
- 编译产物放入独立输出目录，不污染源码树。
- `MEUOS_SYSROOT` 环境变量控制安装目标路径。

---

## 6. 实现策略

### 6.1 分阶段实现原则

**先标准化可用，再增强优化。** 所有组件遵循三阶段实现路径：

1. **标准化可用**：实现 ISO/POSIX 标准定义的核心接口，确保正确性和可用性。不追求完整覆盖标准中的每个边缘特性，但已实现的部分必须行为正确、可被 mcc 自编译。
2. **有利特性与简化接口**：在标准化基础上，增加 MeuOS Next 实际需要的便利特性。因为是 Kit 自己实现的，可以针对 MeuOS 的使用场景设计更简洁的 API、更方便的命令行接口，或省略不适用于 MeuOS 的标准冗余。
3. **性能优化**：在功能和接口稳定后，针对 MeuOS 的实际工作负载做性能优化。可以利用 Kit 自身的架构知识做针对性优化（如 mcc 了解 mt/as 的编码偏好、meow 了解 mcc 的编译速度特性等）。

**必要性简化**：因为 MeuOS Kit 是我们自己的工具集，可以对不适用于 MeuOS Next 的标准内容进行简化。例如：
- 省略标准中 MeuOS 不需要的 locale/i18n 子集（初期只支持 C locale）
- 省略不使用的 POSIX 选项（如 POSIX 消息队列、实时调度选项等）
- 简化错误处理路径（初期用简洁的 abort/panic，后续再完善）
- 工具命令行只实现 MeuOS 实际需要的选项子集

但所有简化必须有据可查，记录在各子项目的 `.todo/` 或 `ARCHITECTURE.md` 中。

### 6.2 参考资源（节省算力）

**核心原则：优先参考成熟社区实现，避免从零推导繁琐算法而浪费算力。**

#### 本仓库已提供的只读参考树（`reference/`，gitignored，勿改勿提交）

| 路径 | 用途 |
|------|------|
| `reference/cproc/` | mcc 前端设计参考（词法/语法/语义/类型系统） |
| `reference/qbe/`   | mcc 后端设计参考（IR/指令选择/寄存器分配/各 arch emit） |
| `reference/musl/`   | meuos-libc 算法参考（mallocng/stdio/pthread/...） |
| `reference/tinycc/` | 轻量 C 编译器参考（快速编译、简单后端、tcc 的 preprocessor） |

#### 鼓励参考的其他社区资源

- **libc 算法**：musl（首选，已 vendored）、Cosmopolitan Libc、serenityOS LibC、PDCLib
- **编译器设计**：cproc/QBE（已 vendored）、chibicc、9cc、lacc、cparser
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
