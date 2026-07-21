# 待规划：MeuOS Kit 工具链（汇编器 / 链接器 / 归档器）

## 背景

mcc 当前生成的 `.s` 汇编文本和最终的链接工作都外包给宿主 `cc`：
- `src/driver/host_toolchain.c:run_host_cc()` 调用 `cc -x assembler <file> -c`
  做汇编，`cc <file> -o <out>` 做汇编+链接
- `src/driver/host_toolchain.c:run_host_link()` 调用 `cc <.o files> -L -l -o`
  做 .o + .a 链接
- `projects/mcc/Makefile` 用宿主 `ar rcs` 打包 `libmcc.a`

这三处是 MeuOS Kit 自举链的**最后外部依赖**。AGENTS.md §4 强约束
（零 GNU 代码）要求最终由 Kit 自身工具完成 `.s → .o → 可执行`。

## 必须自研的工具清单

按依赖关系和复杂度分三档：

### 第一档：核心工具链（自举必需）

| 工具 | 替代 | 输入 → 输出 | 复杂度 | 必要性 |
|------|------|------------|--------|--------|
| **mar** | `ar rcs` + `ranlib` | `.o[] → .a`（含符号索引） | 低 | P0 |
| **masm** | `cc -x assembler -c` / `as` | `.s → .o`（ELF 可重定位） | 中 | P1 |
| **mld** | `cc <.o> -o` / `ld` | `.o[] + .a[] → ELF 可执行/共享库` | 高 | P2 |

### 第二档：辅助二进制工具（调试/优化用，可后期）

| 工具 | 替代 | 用途 |
|------|------|------|
| **mnm** | `nm` | 列符号表（调试 mld/masm） |
| **mobjdump** | `objdump -d` | 反汇编（调试代码生成） |
| **mreadelf** | `readelf` | 解析 ELF 头/节区/程序头 |
| **mstrip** | `strip` | 去 .symtab/.strtab |
| **mobjcopy** | `objcopy` | 节区复制/格式转换 |

### 第三档：扩展工具（长期，可选）

| 工具 | 替代 | 用途 |
|------|------|------|
| **mlib** | 不需要 | （mar 覆盖） |
| **mstrings** | `strings` | 提取可打印字符串 |
| **msize** | `size` | 列节区大小 |
| **maddr2line** | `addr2line` | 调试地址 → 源码行 |

## 项目组织方案对比

### 方案 A：单项目产出多二进制（meuos-binutils）
```
projects/binutils/
├── src/{mar,masm,mld,mnm,mobjdump,...}.c
├── src/libelf/         # 内部公共库
├── Makefile            # 一次构建所有工具
└── 产出: mar, masm, mld, mnm, ...
```
**优点**：一次 `make` 出全部；公共代码天然在一起
**缺点**：单项目臃肿；masm 和 mld 复杂度差异大，演进节奏不同；
独立测试和独立 .todo 难以维护
**类似**：GNU binutils（ar/as/ld/objdump 一个包）

### 方案 B：完全独立子项目
```
projects/mar/    projects/masm/    projects/mld/    projects/mnm/
└── 自包含 ELF 解析代码
```
**优点**：每个工具完全独立
**缺点**：ELF 解析/符号表/重定位代码三处重复；维护成本高
**类似**：早期 tinylinker + tinyas 各自独立

### 方案 C（推荐）：独立子项目 + 共享 libelf 公共库
```
projects/
├── libelf/                 # 公共库：ELF/符号/重定位/节区操作
│   ├── include/libelf/
│   │   ├── elf.h           # ELF 常量（DT_*, SHT_*, R_X86_64_* 等）
│   │   ├── symbol.h        # 符号表抽象
│   │   ├── reloc.h         # 重定位类型抽象
│   │   └── section.h       # 节区抽象
│   └── src/{elf,symbol,reloc,section}.c
├── mar/                    # 归档器（依赖 libelf，最简单）
├── masm/                   # 汇编器（依赖 libelf，中等）
├── mld/                    # 链接器（依赖 libelf，最复杂）
├── mnm/                    # 符号查看器（依赖 libelf，简单）
├── mobjdump/               # 反汇编器（依赖 libelf + libmcc 的 disassembler）
└── mreadelf/               # ELF 查看器（依赖 libelf，简单）
```
**优点**：
- 公共代码集中（ELF 解析只写一次）
- 每个工具独立 Makefile / .todo / check（符合 Kit 现有规范）
- 独立演进节奏（mar 先做、mld 后做）
- 自举清晰：每个工具都能用 mcc + libc-meuos 自重编译
- 与已有的 libmcc 模式一致（共享库 + 独立二进制）
**缺点**：libelf 接口设计需要前期投入
**类似**：LLVM（libLLVM 共享 + 各工具独立）

## 推荐方案 C 的实施路径

### 阶段 1：libelf + mar（最低成本起步，立即替换宿主 ar）
- `projects/libelf/` — ELF 常量/解析/节区操作最小集
- `projects/mar/` — `mar rcs libfoo.a foo.o bar.o`
  - 实现 BSD/GNU ar 格式（`!<arch>\n` magic + 60 字节 header）
  - 内置 ranlib 功能（生成 `__.SYMDEF` 或 `/` 符号索引节）
- **验收**：`mar rcs build/libmcc.a <fe/oe objs>` 产出与宿主 `ar rcs`
  字节级等价（或符号级等价）；`make -C projects/mcc check` 全绿

### 阶段 2：masm（x86_64 优先，逐步扩展）
- `projects/masm/` — 输入 `.s`（mcc 生成的 AT&T 或 Intel 语法）
- 第一里程碑：x86_64 整数指令 + 重定位
  - 解析 mcc 输出的 `.s`（已知语法子集，比 gas 简单）
  - 生成 ELF64 `.o`：`.text` + `.symtab` + `.strtab` + `.rela.text`
  - 支持 R_X86_64_PC32 / R_X86_64_PLT32 / R_X86_64_64 / R_X86_64_32S
- 第二里程碑：x86_64 浮点（SSE/AVX）
- 第三里程碑：aarch64（与 mcc aarch64 后端配合）
- 第四里程碑：i386 / riscv64 / loongarch64
- **验收**：`masm foo.s -o foo.o` 产出的 `.o` 可被宿主 `ld` 链接；
  最终用 `mld` 链接通过

### 阶段 3：mld（静态链接优先，动态后期）
- `projects/mld/` — 输入 `.o[]` + `.a[]`
- 第一里程碑：静态链接 x86_64
  - 符号解析（强/弱符号、重复定义检测）
  - 节区合并（`.text`/`.data`/`.bss`/`.rodata` 同类合并）
  - 重定位应用（按 `R_X86_64_*` 类型计算最终地址）
  - 程序头生成（PT_LOAD / PT_INTERP / PT_DYNAMIC）
  - ELF 可执行文件输出（含 entry、`_start` 解析）
  - TLS 模板处理（`.tdata`/`.tbss`）
- 第二里程碑：aarch64 / i386 静态链接
- 第三里程碑：动态链接（`.dynsym` / PLT / GOT / R_X86_64_GLOB_DAT）
- **验收**：`mld foo.o -lc-meuos -o foo` 产出的可执行文件可在
  MeuOS / Linux 上运行；`counter = 2000` 等价闭环通过

### 阶段 4：辅助工具（按需）
- `mnm` / `mreadelf` / `mobjdump` / `mstrip` / `mobjcopy`
- 优先级低，主要用于调试 masm/mld 产出
- 部分可由 mar/mld 内部 `--dump` 选项覆盖

## 自研优先级排序（建议路线）

| 优先级 | 工具 | 理由 |
|--------|------|------|
| **P0** | libelf + mar | 最简单、立即替换宿主 ar、为 masm/mld 铺路 |
| **P1** | masm (x86_64) | 解除 mcc 对 `cc -c` 的依赖；x86_64 是主开发架构 |
| **P2** | mld (x86_64 静态) | 解除 mcc 对 `cc link` 的依赖；完成自举链 |
| **P3** | masm (aarch64/i386) | 扩展架构覆盖，配合 aarch64 移植 |
| **P4** | mld (aarch64/i386 静态) | 多架构自举 |
| **P5** | mnm / mreadelf | 调试工具，提升 masm/mld 开发效率 |
| **P6** | masm/mld (riscv64/loongarch64) | 完整架构覆盖 |
| **P7** | mld 动态链接 | 支持 DSO、共享库 |
| **P8** | mobjdump / mstrip / mobjcopy | 完整 binutils 套件 |

## 与现有 Kit 组件的关系

```
┌─────────────────────────────────────────────────────────┐
│  meow (构建系统)                                          │
│    调用 mcc / masm / mld / mar                            │
└─────────────────────────────────────────────────────────┘
          │            │           │          │
          ▼            ▼           ▼          ▼
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│   mcc    │  │  masm    │  │   mld    │  │   mar    │
│ C→.s     │  │ .s→.o    │  │ .o→exec  │  │ .o[]→.a  │
└──────────┘  └──────────┘  └──────────┘  └──────────┘
   │  │           │             │              │
   │  └─libmcc    └─libelf──────┴──────────────┘
   │
   └─meuos-libc (提供运行时 + crt1.o)
```

- **libmcc**：mcc 的共享后端（m++ 也用），阶段 A 已完成
- **libelf**：masm/mld/mar/mnm 的公共库（ELF 解析、符号、重定位）
- mobjdump 反汇编可复用 mcc 后端的指令解码（额外抽象）

## 参考实现（许可证友好）

### libelf
- **musl** `src/internal/elf.h` — ELF 常量定义（MIT，可直接借用）
- **serenityOS** `Libraries/LibELF` — ELF 解析（BSD）
- **PDCLib** — 简化 ELF 处理（公有领域）
- 不参考：GNU libelf（GPL）

### masm（汇编器）
- **fasm** — 自有许可，x86 多架构汇编器
- **NASM/YASM** — BSD 许可，x86/x86_64
- **gas** — GPL，**不可参考代码**，但可参考语法兼容性
- **llvm-mc** — Apache 2.0，可参考架构覆盖思路
- **tinyasm** / **chibiasm** — 教学用极简汇编器
- 注意：mcc 生成的 `.s` 是已知语法子集（GNU AT&T 风格 + 苹果变体），
  masm 只需覆盖 mcc 实际输出的指令，比通用汇编器简单

### mld（链接器）
- **LLD** — Apache 2.0，完整生产级链接器（参考架构设计）
- **blink** (jart) — ISC，含简化 linker，x86_64 主导
- **sa_main** (davidar) — 极简静态链接器，教学参考
- **tinylinker** (jnz) — 极简 ELF 静态链接器
- **serenityOS** `Libraries/LibELF` + `Userland/Utilities/ld` — 完整链接器实现
- **Pedigree OS** linker — 嵌入式级简化
- 不参考：GNU ld（GPL）

### mar（归档器）
- **musl** `tools/musl-gcc.specs.sh` — ar 格式最简单参考
- **tinyar** — 极简 ar 实现
- **BSD ar** — BSD 许可，ar/ranlib 实现
- ar 格式极简：`!<arch>\n` + 60 字节 header + 文件数据，几百行 C 即可

## 影响范围

### 新增
- `projects/libelf/` — 公共库
- `projects/mar/` — 归档器
- `projects/masm/` — 汇编器
- `projects/mld/` — 链接器
- （后续）`projects/mnm/` / `mreadelf/` / `mobjdump/` / `mstrip/` / `mobjcopy/`

### 修改
- `projects/mcc/Makefile` — `AR ?= ar` 改为优先使用 `mar`
  （`AR ?= mar || ar`，bootstrap 期仍允许宿主 ar）
- `projects/mcc/src/driver/host_toolchain.c` — `MCC_HOST_CC` 旁加
  `MCC_MASM` / `MCC_MLD` 环境变量，自举路径优先用 masm/mld
- `STATE.md` §1 — 增加工具链组件状态
- `STATE.md` §4 — 增 P10 工具链自研优先级
- `AGENTS.md` §2 — 增加 masm/mld/mar 组件规范

### 不修改
- `projects/mcc/` 的核心代码生成逻辑（mcc 仍输出 `.s`，由 masm 接管）

## 前置依赖
- libmcc 阶段 A 完成（已完成，验证了库拆分模式可行）
- mcc 各架构代码生成稳定（x86_64 已稳定，aarch64/i386 有 bug 待修）

## 验收
- **libelf + mar**：`mar rcs` 产出的 `.a` 可被宿主 `ld` 接受；
  `make -C projects/mcc check` 全绿
- **masm x86_64**：`masm foo.s -o foo.o` 产出的 `.o` 可被宿主 `ld`
  链接成可运行可执行文件
- **mld x86_64 静态**：`mld foo.o -lc-meuos -o foo` 产出的可执行文件
  在 MeuOS 上运行输出 `counter = 2000`
- **完整自举**：mcc + masm + mld + mar 全部由 mcc + libc-meuos 自重编译
  通过；Kit 自举链零宿主依赖

## 备注
- **masm 的范围控制**：mcc 生成的 `.s` 是已知语法子集（不手写汇编），
  masm 只需覆盖 mcc 实际输出 + 少量手写 `.S`（crt1.S/atomic.S 等运行时）。
  不必实现完整 gas 兼容。
- **mld 的范围控制**：静态链接先行，动态链接 P7。MeuOS Next 的 DSO
  需求由后续 ldso + 动态 mld 协同。
- **mar 的兼容性**：实现 GNU/BSD ar 格式（`<name>/` 末尾斜杠的 GNU 扩展），
  与宿主 `ld` 互操作无障碍。
- **不阻塞当前工作**：aarch64 移植和 libmcc 阶段 B 都不依赖工具链自研。
  工具链自研可在 aarch64 完成后启动，或并行启动 mar（最简单）。
