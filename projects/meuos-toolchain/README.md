# meuos-toolchain (mt)

> MeuOS Kit 的工具链项目：汇编器、链接器、归档器、二进制工具整套提供。
> 解除 mcc 对宿主 `cc`/`as`/`ld`/`ar` 的最后依赖，完成 Kit 自举链。

## 项目定位

**单项目整套提供**：汇编器、链接器、归档器、二进制工具全部在
`projects/meuos-toolchain/` 一个项目里，一次 `make` 构建全套。

- **项目名**：`meuos-toolchain`（简称 `mt`）
- **二进制无前缀**：MeuOS 环境里这些是唯一工具，不需要 `m-` 前缀区分
    - `as` — 汇编器
    - `ld` — 链接器
    - `ar` — 归档器
    - `nm` / `objdump` / `readelf` / `strip` / `objcopy` — 二进制工具
- **内部模块化**：`src/<tool>/`（工具核心）+ `src/libelf/`（内部共享库）+ `src/target/<arch>/`（架构相关）
- **与 libmcc 的区别**：libmcc 是跨二进制共享（mcc/m++），需要稳定 API；
  mt 是单项目内部共享，libelf 接口可以随意演进
- **架构区分**：单一二进制 + `--target` 选项 / 从输入自动推断（与 mcc 一致），
  架构差异集中在 `src/target/<arch>/`（与 mcc `src/target/` 对齐），
  不产出多个二进制

## 背景

mcc 当前生成的 `.s` 汇编文本和最终的链接工作都外包给宿主 `cc`：

- `src/driver/host_toolchain.c:run_host_cc()` 调用 `cc -x assembler <file> -c`
  做汇编，`cc <file> -o <out>` 做汇编+链接
- `src/driver/host_toolchain.c:run_host_link()` 调用 `cc <.o files> -L -l -o`
  做 .o + .a 链接
- `projects/mcc/Makefile` 用宿主 `ar rcs` 打包 `libmcc.a`

这三处是 MeuOS Kit 自举链的**最后外部依赖**。AGENTS.md §4 强约束
（零 GNU 代码）要求最终由 Kit 自身工具完成 `.s → .o → 可执行`。

## 目录结构

```
projects/meuos-toolchain/
├── README.md               # 本文件
├── Makefile                # 一次构建所有工具
├── ARCHITECTURE.md         # 分阶段架构、任务与验收门禁
├── include/                # 项目内部头文件
│   ├── elf.h               # ELF 常量（DT_*, SHT_*, R_X86_64_* 等）
│   ├── symbol.h            # 符号表抽象
│   ├── reloc.h             # 重定位类型抽象
│   └── section.h           # 节区抽象
├── src/
│   ├── libelf/             # 内部公共库（ELF/符号/重定位/节区操作）
│   │   ├── elf.c
│   │   ├── symbol.c
│   │   ├── reloc.c
│   │   └── section.c
│   ├── as/                 # 汇编器核心（.s → .o）
│   │   ├── as.c            # main
│   │   ├── lex.c           # 词法
│   │   └── parse.c         # 语法（AT&T + Intel 双语法）
│   ├── ld/                 # 链接器核心（.o + .a → 可执行/共享库）
│   │   ├── ld.c            # main
│   │   ├── resolve.c       # 符号解析
│   │   ├── merge.c         # 节区合并
│   │   ├── reloc_apply.c   # 重定位应用（分发到 target/<arch>/）
│   │   ├── layout.c        # 地址布局
│   │   └── output.c        # ELF 输出
│   ├── ar/                 # 归档器（.o[] → .a，含 ranlib 功能）
│   │   └── ar.c
│   ├── nm/                 # 符号表查看
│   │   └── nm.c
│   ├── readelf/            # ELF 查看
│   │   └── readelf.c
│   ├── objdump/            # 反汇编（复用 target/<arch>/disas.c）
│   │   └── objdump.c
│   ├── strip/              # 去符号表
│   │   └── strip.c
│   ├── objcopy/            # 节区复制/格式转换
│   │   └── objcopy.c
│   └── target/             # 架构相关代码集中（与 mcc src/target/ 对齐）
│       ├── x86_64/
│       │   ├── as_emit.c   # 汇编指令编码
│       │   ├── ld_reloc.c  # 重定位应用
│       │   └── disas.c     # 反汇编
│       ├── aarch64/
│       │   ├── as_emit.c
│       │   ├── ld_reloc.c
│       │   └── disas.c
│       ├── i386/
│       │   ├── as_emit.c
│       │   ├── ld_reloc.c
│       │   └── disas.c
│       ├── loongarch64/
│       │   ├── as_emit.c
│       │   ├── ld_reloc.c
│       │   └── disas.c
│       ├── riscv64/
│       │   ├── as_emit.c
│       │   ├── ld_reloc.c
│       │   └── disas.c
│       ├── armv7/          # 待实现（mcc 占位中）
│       ├── powerpc64le/    # 待实现（mcc 占位中）
│       └── s390x/          # 待实现（mcc 占位中）
├── test/                   # 各工具的测试用例
│   ├── as/
│   ├── ld/
│   └── ar/
└── build/                  # 编译产物（gitignored）
    ├── as
    ├── ld
    ├── ar
    ├── nm
    ├── objdump
    ├── readelf
    ├── strip
    └── objcopy
```

## 工具清单与优先级

### 第一档：核心工具链（自举必需）

| 工具   | 替代                        | 输入 → 输出                       | 复杂度 | 优先级 |
| ------ | --------------------------- | --------------------------------- | ------ | ------ |
| **ar** | `ar rcs` + `ranlib`         | `.o[] → .a`（含符号索引）         | 低     | **P0b** |
| **as** | `cc -x assembler -c` / `as` | `.s → .o`（ELF 可重定位）         | 中     | **P1** |
| **ld** | `cc <.o> -o` / `ld`         | `.o[] + .a[] → ELF 可执行/共享库` | 高     | **P2** |

### 第二档：辅助二进制工具（调试/优化用）

| 工具        | 替代         | 用途                        | 优先级 |
| ----------- | ------------ | --------------------------- | ------ |
| **nm**      | `nm`         | 列符号表（调试 ld/as 必备） | P5     |
| **readelf** | `readelf`    | 解析 ELF 头/节区/程序头     | P5     |
| **objdump** | `objdump -d` | 反汇编（调试代码生成）      | P5     |
| **strip**   | `strip`      | 去 `.symtab`/`.strtab`      | P8     |
| **objcopy** | `objcopy`    | 节区复制/格式转换           | P8     |

### 第三档：扩展工具（长期，可选）

| 工具          | 替代        | 用途              |
| ------------- | ----------- | ----------------- |
| **size**      | `size`      | 列节区大小        |
| **addr2line** | `addr2line` | 调试地址 → 源码行 |
| **strings**   | `strings`   | 提取可打印字符串  |

## 目标架构清单

mt 的架构支持与 mcc 的 `src/target/` 完全对齐。架构状态分为四档：

| 架构            | 状态         | mcc 实现           | mt 实现优先级        | 说明                                                     |
| --------------- | ------------ | ------------------ | -------------------- | -------------------------------------------------------- |
| **x86_64**      | 已确认基石   | ✅ 已实现          | **P1**（首批）       | 主开发平台，参考 musl 最成熟分支                         |
| **aarch64**     | 已确认基石   | ✅ 已实现          | **P3**（首批扩展）   | 64 位 ARM，覆盖手机/开发板，验证跨指令集移植             |
| **loongarch64** | 已确认基石   | ✅ 已实现          | **P6**（第二批扩展） | 龙芯新生态，需紧跟最新 ABI 与内核接口                    |
| **i386**        | 已确认基石   | ✅ 已实现          | **P3**（首批扩展）   | 32 位 x86，借自主 libc 强制 64 位 time_t，根除 2038 问题 |
| **riscv64**     | 强烈建议新增 | ✅ 已实现          | **P6**（第二批扩展） | 全新无历史包袱，检验 libc 架构抽象是否足够"干净"         |
| **armv7**       | 强烈建议新增 | ⬜ 占位（`.todo`） | P9（跟随 mcc）       | 海量 32 位 ARMv7 设备，验证 32/64 位变体与浮点 ABI       |
| **powerpc64le** | 按需可选     | ⬜ 占位（`.todo`） | P10（跟随 mcc）      | POWER 服务器，挑战多寄存器调用约定                       |
| **s390x**       | 按需可选     | ⬜ 占位（`.todo`） | P10（跟随 mcc）      | IBM 大型机，挑战独特 syscall 机制                        |
| ~~armel~~       | 明确跳过     | —                  | 不做                 | 极老软浮点 ARM，有 armv7 后已无必要                      |
| ~~mips\*~~      | 明确跳过     | —                  | 不做                 | 生态萎缩，龙芯已转向 LoongArch                           |

**mt 与 mcc 的架构同步原则**：

- mcc 已实现的架构，mt 必须跟进（`as` 要能汇编 mcc 生成的 `.s`，`ld` 要能链接 mcc 产出的 `.o`）
- mcc 仅占位的架构（armv7/powerpc64le/s390x），mt 跟随 mcc 节奏，不提前实现
- mcc 明确跳过的架构（armel/mips\*），mt 同样不做

## 架构区分策略

mt 与 mcc 保持一致：**单一二进制 + 架构在源码层模块化**，不产出每架构一个二进制。
支持的完整架构清单见上一节。

各工具的架构相关性不同，选择策略也不同：

| 工具      | 架构相关性                                | 架构选择方式                                           | 源码模块化                 |
| --------- | ----------------------------------------- | ------------------------------------------------------ | -------------------------- |
| `ar`      | 完全无关（只打包字节流，不解析 ELF 内容） | 不需要                                                 | 单一 `ar.c`                |
| `as`      | 强相关（指令编码各架构完全不同）          | `--target=<triplet>` 选项                              | `target/<arch>/as_emit.c`  |
| `ld`      | 弱相关（重定位类型与计算公式不同）        | 从输入 `.o` 的 ELF header 自动推断（`e_machine` 字段） | `target/<arch>/ld_reloc.c` |
| `nm`      | 无关（只读符号表）                        | 不需要                                                 | 单一 `nm.c`                |
| `readelf` | 无关（只读不解释指令）                    | 不需要                                                 | 单一 `readelf.c`           |
| `objdump` | 反汇编时相关（要解码指令）                | 从输入 ELF 自动推断                                    | `target/<arch>/disas.c`    |
| `strip`   | 无关                                      | 不需要                                                 | 单一 `strip.c`             |
| `objcopy` | 无关（节区复制不解释内容）                | 不需要                                                 | 单一 `objcopy.c`           |

**关键设计**：

- `as` 是唯一需要显式 `--target` 的工具（输入是文本，无法从输入推断目标架构）
- `ld` / `objdump` 从输入 `.o` 的 ELF header（`e_machine`）自动推断，不需要 `--target`
- `ar` / `nm` / `readelf` / `strip` / `objcopy` 完全架构无关
- 这与 GNU `as` 需要 `--target` 而 `ld`/`objdump` 自动推断的行为一致

## 实施路径

### 阶段 0：项目骨架 + libelf（P0 前置，已启动）

- 创建 `projects/meuos-toolchain/` 目录结构
- `src/libelf/` 首期实现 ELF64 little-endian magic/class/encoding/version 和表范围验证
- 使用项目自有格式常量，不包含宿主 `<elf.h>`
- **当前验收**：`make -C projects/meuos-toolchain check`，`elf_probe` 能识别宿主 `cc` 产出的 `ET_REL/EM_X86_64` `.o`

### 阶段 1：ar 基础读写（P0a，已完成）

- `src/ar/` 实现 `ar rcs libfoo.a foo.o bar.o`、`t`、`p`、`x`
    - SysV short-name 格式（`!<arch>\n` magic + 60 字节 header）
    - 固定 mtime/uid/gid，保证输出可复现
- **验收**：创建、列出、打印、解出成员并逐字节比较；P0b 继续增加宿主 `ld` 链接门禁

### 阶段 1b：ar 完整化（P0b，核心完成）

- 成员替换和追加语义
- GNU/BSD long-name table
- 内置 ranlib 功能（生成 `/` 符号索引节）
- **验收**：`ar rcs build/libmcc.a <objs>` 可被宿主 `ld` 接受；独立 `ar s` 可重建索引；mcc Makefile 自动替换留到 P3

### 阶段 2：as（P1，x86_64 核心已完成，逐步扩展）

- `src/as/` 输入 `.s`（mcc 生成的 AT&T 语法 + 手写 `.S`）
- 第一里程碑：x86_64 整数指令 + 重定位
    - 解析 mcc 输出的 `.s`（已知语法子集，比 gas 简单）
    - 生成 ELF64 `.o`：`.text` + `.symtab` + `.strtab` + `.rela.text`
    - 支持 R_X86_64_PC32 / R_X86_64_PLT32 / R_X86_64_64 / R_X86_64_32S
- 第二里程碑：x86_64 浮点（SSE/AVX）
- 第三里程碑：aarch64（与 mcc aarch64 后端配合）
- 第四里程碑：i386 / riscv64 / loongarch64
- **验收**：`as foo.s -o foo.o` 产出的 `.o` 可被宿主 `ld` 链接；
  最终用 mt 的 `ld` 链接通过

### 阶段 3：ld（P2，x86_64 静态核心已完成，动态后期）

- `src/ld/` 输入 `.o[]` + `.a[]`
- 第一里程碑：静态链接 x86_64
    - 符号解析（强/弱符号、重复定义检测）
    - 节区合并（`.text`/`.data`/`.bss`/`.rodata` 同类合并）
    - 重定位应用（按 `R_X86_64_*` 类型计算最终地址）
    - 程序头生成（PT_LOAD / PT_INTERP / PT_DYNAMIC）
    - ELF 可执行文件输出（含 entry、`_start` 解析）
    - TLS 模板处理（`.tdata`/`.tbss`）
- **当前 x86_64 静态核心已覆盖**：ET_REL 合并、全局/弱符号、归档输入、`.text/.rodata/.got/.data/.bss` 布局、R_X86_64_64/PC32/PLT32/GOTPCREL/32/32S；
- 第二里程碑：aarch64 / i386 静态链接
- 第三里程碑：动态链接（`.dynsym` / PLT / GOT / R_X86_64_GLOB_DAT）
- **验收**：`ld foo.o -lc-meuos -o foo` 产出的可执行文件可在
  MeuOS / Linux 上运行；`counter = 2000` 等价闭环通过

### 阶段 4：辅助工具（P5-P8，按需）

- `nm` / `readelf` / `objdump` — 调试 as/ld 产出
- `strip` / `objcopy` — 后期优化
- 部分可由 as/ld 内部 `--dump` 选项覆盖

## 优先级路线图

| 优先级 | 工具                        | 理由                                            |
| ------ | --------------------------- | ----------------------------------------------- |
| **P0a/P0b** | libelf + ar                 | 已完成格式层、symbol index、长名和宿主链接互操作 |
| **P1** | as (x86_64)                 | 已完成 mcc 常见 x86_64 汇编子集和 ET_REL 输出   |
| **P2** | ld (x86_64 静态)            | 已完成静态核心，下一步接入 MeuOS sysroot       |
| **P3** | as (aarch64/i386)           | 扩展架构覆盖，配合 aarch64 移植                 |
| **P4** | ld (aarch64/i386 静态)      | 多架构自举                                      |
| **P5** | nm / readelf                | 调试工具，提升 as/ld 开发效率                   |
| **P6** | as/ld (riscv64/loongarch64) | 完整架构覆盖                                    |
| **P7** | ld 动态链接                 | 支持 DSO、共享库                                |
| **P8** | objdump / strip / objcopy   | 完整工具链套件                                  |

## 与现有 Kit 组件的关系

```
┌─────────────────────────────────────────────────────────┐
│  meow (构建系统)                                          │
│    调用 mcc / as / ld / ar                                │
└─────────────────────────────────────────────────────────┘
          │            │           │          │
          ▼            ▼           ▼          ▼
┌──────────┐  ┌──────────────────────────────────────┐
│   mcc    │  │  meuos-toolchain (mt)                │
│ C→.s     │  │  ├── as  (.s → .o)                   │
└──────────┘  │  ├── ld  (.o + .a → 可执行)            │
   │          │  ├── ar  (.o[] → .a)                  │
   │  └libmcc │  ├── nm / readelf / objdump / strip   │
   │          │  └── 内部 libelf（不对外暴露）          │
   ▼          └──────────────────────────────────────┘
┌──────────────────────────────────────────────────┐
│  meuos-libc (提供运行时 + crt1.o)                 │
└──────────────────────────────────────────────────┘
```

## 与 mcc 的集成方式

### bootstrap 期（宿主上构建 mcc）

- mcc Makefile 仍用宿主 `ar`（`AR ?= ar`）
- mcc driver 仍用宿主 `cc` 做 as/ld（`MCC_HOST_CC` 环境变量）
- mt 项目独立构建，产出在 `projects/meuos-toolchain/build/`

### 自举期（mt 完成后）

- mcc Makefile 改为优先用 mt 的 ar：
  `AR ?= projects/meuos-toolchain/build/ar`
- mcc driver 通过环境变量 `MT_AS` / `MT_LD` / `MT_AR` 找到 mt 工具
  （fallback 到宿主工具，保证 bootstrap 期不破）

### 安装到 sysroot

- mt 工具安装到 `${MEUOS_SYSROOT}/usr/bin/{as,ld,ar,nm,...}`
- MeuOS 环境里这些是唯一工具，无前缀

## 参考实现（许可证友好）

### libelf

- **musl** `src/internal/elf.h` — ELF 常量定义（MIT，可直接借用）
- **serenityOS** `Libraries/LibELF` — ELF 解析（BSD）
- **PDCLib** — 简化 ELF 处理（公有领域）
- 不参考：GNU libelf（GPL）

### as（汇编器）

- **fasm** — 自有许可，x86 多架构汇编器
- **NASM/YASM** — BSD 许可，x86/x86_64
- **gas** — GPL，**不可参考代码**，但可参考语法兼容性
- **llvm-mc** — Apache 2.0，可参考架构覆盖思路
- 注意：mcc 生成的 `.s` 是已知语法子集（GNU AT&T 风格 + 苹果变体），
  as 只需覆盖 mcc 实际输出的指令，比通用汇编器简单

### ld（链接器）

- **LLD** — Apache 2.0，完整生产级链接器（参考架构设计）
- **blink** (jart) — ISC，含简化 linker，x86_64 主导
- **sa_main** (davidar) — 极简静态链接器，教学参考
- **tinylinker** (jnz) — 极简 ELF 静态链接器
- **serenityOS** `Userland/Utilities/ld` — 完整链接器实现
- 不参考：GNU ld（GPL）

### ar（归档器）

- ar 格式极简：`!<arch>\n` + 60 字节 header + 文件数据，几百行 C 即可
- 参考 BSD ar（BSD 许可）的 ar/ranlib 实现

## 前置依赖

- libmcc 阶段 A 完成（已完成，验证了库拆分模式可行）
- mcc 各架构代码生成稳定（x86_64 已稳定，aarch64/i386 有 bug 待修）

## 验收

- **P0a/P0b 当前完成**：`make -C projects/meuos-toolchain check` 全绿；`libelf` 可验证 x86_64 ET_REL 并读取 symtab/strtab；`ar` 支持 symbol index、GNU long-name table、r/q 更新语义和宿主 `ld` 互操作；
- **尚未完成**：独立 `ranlib` 命令、BSD `#1/` extended-name 变体、浮点/SSE 完整编码、mcc Makefile/driver 集成；
- **as x86_64**：`make -C projects/meuos-toolchain check-as-x86_64 check-as-libc-x86_64` 通过；mcc 整数/数据/调用 fixture 与 MeuOS libc x86_64 运行时汇编均可生成 ET_REL；
- **ld x86_64 核心**：`make -C projects/meuos-toolchain check-ld-x86_64` 通过，mt as + ar + ld + GOT/PLT + syscall smoke 可运行；
- **尚未完成的 P2 生产门禁**：MeuOS libc/crt1/sysroot 静态链接和 `counter = 2000`，需要下一阶段接入 sysroot
- **完整自举**：mcc + mt（as + ld + ar）全部由 mcc + libc-meuos 自重编译
  通过；Kit 自举链零宿主依赖

## 备注

- **as 的范围控制**：mcc 生成的 `.s` 是已知语法子集（不手写汇编），
  as 只需覆盖 mcc 实际输出 + 少量手写 `.S`（crt1.S/atomic.S 等运行时）。
  不必实现完整 gas 兼容。
- **ld 的范围控制**：静态链接先行，动态链接 P7。MeuOS Next 的 DSO
  需求由后续 ldso + 动态 ld 协同。
- **ar 的兼容性**：P0a/P0b 已实现 SysV/GNU short-name、GNU `//` long-name table 和
  `/` symbol index；BSD `#1/` extended-name 及独立 ranlib 留在后续兼容性任务。
- **不阻塞当前工作**：aarch64 移植和 libmcc 阶段 B 都不依赖工具链自研。
  mt 的 ar 可并行启动（最简单、自包含）。

## 当前实施状态（2026-07-22）

首期开发固定为 **x86_64 优先**，当前已完成 P0a/P0b、P1 核心和 P2 静态链接基础：

- 独立 `Makefile`，源码、依赖文件和产物隔离到 `build/`；
- 内部 `libelf`：不包含宿主 `<elf.h>`，可验证 ELF64 little-endian 头部/节区表，并读取 symtab/strtab；
- `ar` 支持 `rcs`/`q`/`r`/`s`/`t`/`p`/`x`，生成可复现的 SysV/GNU archive；
- 归档包含 `/` symbol index，长成员名使用 `//` table，宿主 `ld` 可以直接链接；
- x86_64 `as` 生成 ET_REL，`ld` 生成 ET_EXEC；回归覆盖 ELF、symbol index、宿主链接、长名、追加、替换、GOT/PLT 和 syscall 运行；
- 阶段计划和门禁见 [`ARCHITECTURE.md`](ARCHITECTURE.md)；
- 当前剩余兼容性任务记录在 `.todo/p0-foundation-ar.md`、`.todo/p1-x86_64-as.md` 和 `.todo/p2-x86_64-ld.md`。

### 本地构建和验收

```sh
make -C projects/meuos-toolchain clean
make -C projects/meuos-toolchain
make -C projects/meuos-toolchain check
```

首期核心二进制位于：

```text
projects/meuos-toolchain/build/bin/{ar,as,ld}
```

P0a/P0b、x86_64 as 核心和静态 ld 核心已完成；当前仍未实现独立 `ranlib`、BSD `#1/`
extended-name、浮点/SSE 完整编码、MeuOS libc sysroot 链接以及 mcc 的自动集成。
