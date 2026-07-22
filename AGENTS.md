# MeuOS Kit — Agent 初始化 Prompt

> Update: 2026-07-16 14:51
>
> IMPORTANT: 全程思考/回复/文档优先使用简体中文
>
> **会话恢复**：因不可抗力会话可能中断。新会话先读对应子项目的 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项），再按需读本文件（项目规约）。各子项目独立维护状态，无全局 STATE 文件。

**项目名称**：MeuOS Kit**项目定位**：MeuOS Next 的独立开发工具集。提供从零自举所需的全部工具：C 库、编译器、构建系统。**核心组件**：

- `meuos-libc` - C 库（C11 + POSIX，零 GNU 依赖；含 compat 兼容层，隔离被滥用的 glibc 扩展符号）
- `mcc` — 编译器（C11，cproc+QBE 源码级整合，单体，不区分前后端）
- `meuos-toolchain` - 工具链（as/ld/ar/ranlib，消除 Kit 对宿主 cc 的最后依赖）
- `meow` — 构建系统（YAML 配方 + Makefile 兼容）

**交付对象**：具备系统编程和编译器经验的大型 AI Agent（兆级上下文）。
**许可**：MIT

---

## 1. 项目目标

构建一套完整的开发工具集，使得：

1. 可以从任意 Linux 宿主（有 gcc 或 tcc）自举出全套 MeuOS Kit 工具。
2. 用自举出的 Kit 工具能够编译出 MeuOS Next 的最小 sysroot。
3. Kit 自身可以在 MeuOS Next 环境中自我重建（自举验证通过）。
4. 整个自举链零 GNU 代码、零 LLVM 代码、零 glibc 依赖。

---

## 2. 组件规范

### 2.1 meuos-libc - C 库

**标准支持**：ISO C11 + POSIX.1-2008 核心接口，利用 `_Atomic`、`_Generic`、`_Thread_local` 等 C11 特性编写干净实现。

**系统调用**：直接封装 Linux 内核 ABI，使用 `syscall()` 或内联汇编，不经过任何中间库。每个系统调用一个独立源文件。

**符号策略**：核心库只暴露标准符号。任何 GNU 扩展符号（`error_at_line`、`obstack`、`argp` 等）放入 `meuos-libc/src/compat/`，作为独立归档 `libc-meuos-compat.a` 提供，核心库不包含。

**初始里程碑**（最小可用来编译 mcc 和 meow）：

- 启动文件：`crt1.o`（`_start` 入口，调用 `main`，处理 `argv`/`envp`）
- 系统调用封装：至少 30 个（read, write, open, close, fork, execve, exit, mmap, munmap, brk, stat, fstat, lseek, getpid, getcwd, chdir, dup, dup2, pipe, waitpid, nanosleep, clock_gettime, getdents64, unlink, mkdir, rmdir, rename, link, symlink, readlink, chmod, access, socket 等）
- 头文件：`<stdlib.h>`, `<stdio.h>`, `<string.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/syscall.h>`, `<sys/types.h>`, `<sys/stat.h>`, `<errno.h>`, `<signal.h>`, `<setjmp.h>`, `<pthread.h>`（最小子集）, `<stdatomic.h>`, `<threads.h>`, `<stdalign.h>`
- 最小 `printf`/`scanf` 家族（支持 `%s`, `%d`, `%x`, `%c`, `%p`，后续扩展）
- 最小 `malloc`/`free`/`realloc`（简单 first-fit 或从 musl 的 `mallocng` 移植精简版）
- 字符串函数全集（`strlen`, `strcpy`, `memcpy`, `memmove`, `memset` 等）

**[非常重要]实现原则**：可以参考 musl 的算法和结构，但必须用自己的代码重新实现，不直接复制 musl 源码。所有实现必须能用 `mcc` 编译（初期 C99 风格即可，逐步迁移到 C11）。

---

### 2.2 mcc - MeuOS C Compiler### 2.3 mcc — MeuOS C Compiler

**架构要求**：**源码级整合** cproc 编译器前端 + QBE 编译器后端。不保留原始项目边界，所有代码在一个统一源码树中，不区分前后端——对外只有 `mcc` 一个可执行文件。

**整合方式**：

- 拆解 cproc 的解析、语义分析、类型检查代码，拆解 QBE 的 IR 定义、指令选择、寄存器分配、汇编输出代码。
- 按统一数据流重组为：词法 → 语法 → AST → 类型检查 → IR 构造 → 指令选择 → 寄存器分配 → 汇编输出。
- **关键**：cproc 原本生成文本 IR 再交给 QBE 解析，这一步必须消除。改为 cproc 的语义阶段直接调用 IR 构造 API，通过函数调用传递，无文本序列化。
- 所有模块共享统一的内存管理、错误报告、符号表。

**C11 支持**：必须完整支持 `_Atomic`、`_Generic`、`_Thread_local`、`_Alignas`、`_Alignof`、`_Noreturn`、`_Static_assert`、匿名结构体/联合体、复合字面量、指定初始化器、变长数组。

**命令行**（gcc/clang 风格，多字母选项用双横线标准形式，单横线形式也兼容）：

```
mcc -o <output> <files...> [options]
options:
  -I<dir>             头文件搜索路径
  -L<dir>             库搜索路径
  -l<lib>             链接库
  --static            静态链接（兼容: -static）
  --shared            生成共享库（兼容: -shared，暂未实现）
  --sysroot=<dir>     系统根目录
  -D<macro>           预定义宏
  -U<macro>           取消宏定义
  -O<level>           优化级别（0-2）
  -g                  生成调试信息（可暂不支持）
  --nostdinc          不搜索标准头文件（兼容: -nostdinc）
  --nostdlib         不链接标准库（兼容: -nostdlib）
  --specs=meuos       使用 MeuOS 默认配置（自动链接 libc-meuos）（兼容: -specs=meuos）
  -c                  编译到 .o，不链接
  -S                  编译到 .s 汇编，不 assemble
  -E                  只预处理
  --target=<triplet>  目标三元组（兼容: -target=）
  -v                  verbose，打印执行的命令
  --version           打印版本信息
  --help              打印帮助
```

单字母短选项（`-c`/`-o`/`-S`/`-E`/`-D`/`-I`/`-O`/`-g`/`-v` 等）和类别前缀选项（`-W<warning>`/`-f<feature>`/`-m<machine>`）保持单横线，符合 POSIX 和 gcc 约定。多字母选项（`--static`/`--nostdlib` 等）使用双横线作为标准形式；单横线形式（`-static`/`-nostdlib`）由 `arg_compat.c` 自动归一化为双横线，仅作 gcc 兼容用途。

**默认行为**：`--specs=meuos` 是默认模式，自动设置 sysroot、头文件路径、库路径，链接 `libc-meuos`。如果需要兼容层，用户显式加 `-l:libc-meuos-compat.a`。

**性能目标**：代码生成质量接近 gcc -O1，编译速度保持快速（cproc+QBE 原生速度即可）。

**自举要求**：mcc 必须能用自己编译自己，即最终在 MeuOS Next 环境中，用 mcc 重新编译 mcc 源码必须成功且生成功能等价的二进制。

---

### 2.3 meow — 构建系统

**配方格式**：YAML。一个包的完整构建描述。

**YAML 配方结构**（草案，Agent 可优化）：

```yaml
name: dash
version: 0.5.12
source: http://gondor.apana.org.au/~herbert/dash/files/dash-0.5.12.tar.gz
sha256: <hash>

env:
    CC: mcc
    CFLAGS: -O2 --sysroot=${MEUOS_SYSROOT}

steps:
    fetch:
        run: wget -c ${source} -O ${DL_DIR}/dash-${version}.tar.gz
    unpack:
        run: tar xf ${DL_DIR}/dash-${version}.tar.gz -C ${BUILD_DIR}
    patch:
        run: |
            cd ${BUILD_DIR}/dash-${version}
            for p in ${PATCH_DIR}/dash/*.patch; do patch -p1 < $p; done
    build:
        run: |
            cd ${BUILD_DIR}/dash-${version}
            ./configure --host=${HOST} --prefix=/usr
            make -j${NPROC}
    install:
        run: make DESTDIR=${SYSROOT} install
```

**Makefile 兼容**：如果当前目录下没有 `meow.yaml`，meow 自动检测是否存在 `Makefile` 或 `GNUmakefile`，若存在则透明地调用 `make` 并传递关键变量：

```
CC=mcc DESTDIR=${MEUOS_SYSROOT} PREFIX=/usr make
```

这样未移植的老软件可以直接用原版 Makefile 构建。

**meow 自身**：用纯 C 编写，依赖 meuos-libc。初期只支持顺序执行（按 steps 定义顺序），后续可扩展依赖 DAG。提供以下命令：

```
meow build <package>     # 构建指定包
meow clean <package>     # 清理
meow list                # 列出可用包
meow --bootstrap         # 自举模式：用宿主编译自己
```

---

## 3. 自举流程

Agent 必须严格遵循以下阶段，每步都要验证：

**Phase 0 — 准备**

- 宿主编译器可用（gcc 或 tcc）。
- 设定 `MEUOS_SYSROOT` 环境变量指向目标根文件系统路径。

**Phase 1 — 诞生 mcc**

- 用宿主编译器编译 mcc 源码，产出第一代 `mcc` 二进制。
- 验证：`mcc` 能编译 `int main(){return 0;}` 并输出可执行文件（用宿主 libc 运行即可）。

**Phase 2 - 诞生 meuos-libc**

- 用 Phase 1 的 `mcc` 编译 meuos-libc + meuos-libc-compat。
- 安装到 `${MEUOS_SYSROOT}/lib` 和 `${MEUOS_SYSROOT}/include`。
- 验证：用 `mcc -specs=meuos` 编译一个测试程序，链接 libc-meuos，在宿主上能运行或通过 qemu 运行。

**Phase 3 — 诞生 meow**

- 用 `mcc` + `meuos-libc` 编译 meow。
- 安装到 `${MEUOS_SYSROOT}/usr/bin`。
- 验证：`meow build` 能读取一个示例 YAML 配方并执行 fetch + unpack。

**Phase 4 — 自举验证**

- chroot 到 `${MEUOS_SYSROOT}`。
- 用 sysroot 内的 `mcc` + `meow` 重新编译 mcc、meuos-libc、meow。
- 比较两次产物的行为一致性（功能等价即可，不要求 bit 级相同）。
- 全部通过则 MeuOS Kit 初始化完成。

---

## 4. 禁止事项（强约束）

- **禁止**任何 glibc 专有头文件、符号、宏出现在 meuos-libc 核心或 mcc 源码中。
- **禁止**引入 LLVM/Clang 或 GCC 的任何代码。
- **禁止**使用 autotools、cmake、meson 作为 Kit 自身的构建系统（meow 包装它们可以，但 Kit 自身组件必须用简单 Makefile 或 shell 脚本构建）。
- **禁止**系统调用通过 libc 封装，必须直接 `syscall()` 或内联汇编。
- **禁止**预编译二进制提交到仓库（宿主 bootstrapper 除外）。
- **要求**构建可重现（无时间戳、无绝对路径硬编码）。

---

## 5. 项目组织

Agent 自行决定目录布局，须满足：

- 根目录有 `README.md`（项目说明 + 构建方法）。
- 每个组件可独立编译（但共享公共头文件和构建逻辑）。
- 编译产物放入独立输出目录，不污染源码树。
- `MEUOS_SYSROOT` 环境变量控制安装目标路径。

---

## 6. 初始可交付任务（已完成）

初始任务 1-4 已全部完成。后续工作见各子项目 `ARCHITECTURE.md` 路线图。

## 7. 实现策略与参考资源（节省算力）

**核心原则：优先参考成熟社区实现，避免从零推导繁琐算法而浪费算力。**
libc/编译器/构建系统中有大量被反复验证过的算法（内存分配、printf 格式化、
正则、哈希、寄存器分配、SSA 构造、ABI 分类……）。直接参考这些实现的结构与
算法，再用本项目的代码重新实现，远比自行重新推导高效且可靠。

### 本仓库已提供的只读参考树（`reference/`，gitignored，勿改勿提交）

| 路径 | 用途 |
|------|------|
| `reference/cproc/` | mcc 前端设计参考（词法/语法/语义/类型系统） |
| `reference/qbe/`   | mcc 后端设计参考（IR/指令选择/寄存器分配/各 arch emit） |
| `reference/musl/`   | meuos-libc 算法参考（mallocng/stdio/pthread/...） |
| `reference/tinycc/` | 轻量 C 编译器参考（快速编译、简单后端、tcc 的 preprocessor） |

遇到具体实现问题时，先查对应参考树：例如 IR pass 行为看 `reference/qbe/<file>.c`，
libc 的 `printf` 看 `reference/musl/src/stdio/vfprintf.c`，各 arch 的 crt/原子
看 `reference/musl/src/<arch>/`。mcc 的对应副本在 `projects/mcc/src/`，libc 在
`projects/meuos-libc/src/`。

### 鼓励参考的其他社区资源

- **libc 算法**：musl（首选，已 vendored）、Cosmopolitan Libc、serenityOS LibC、
  PDCLib、rlibc——用于 malloc 策略、`<threads.h>`、`<stdatomic.h>` runtime、
  `printf` 格式化内核等。
- **编译器设计**：cproc/QBE（已 vendored）、chibicc、9cc、lacc、cparser——用于
  递归下降解析、类型推导、IR 构造、寄存器分配思路。
- **构建系统**：redo、tup、ninja、bear-make——用于依赖图、增量构建、模式规则。
- **通用知识库**：OSDev Wiki、Linux man-pages、各 arch 的 ELF/ABI spec（SysV
  psABI、AArch64 AAPCS、RISC-V calling convention）。

### 边界（与 §4 禁止事项一致）

- **参考算法与结构，但用本项目自己的代码重新实现**；不直接复制 musl/cproc/QBE
  源码（许可与自举纯洁性要求）。
- 仍受 §4 约束：核心库与 mcc 源码中**禁止** glibc 专有符号、LLVM/Clang、GCC 代码。
- 所有实现必须能被 mcc 自身编译（自举验证），并过对应 `make check`。
- 参考实现的 license（musl 为 MIT、cproc/QBE 为 ISC/MIT、tinycc 为 LGPL）允许
  学习算法，但本项目代码必须是独立撰写、MIT 许可的原创实现。

**一句话**：站在巨人肩膀上——读参考实现理解算法，再用自己的手写出来，不要把
算力花在重新发明轮子上。
