# meuos-toolchain 架构与首期开发计划

> 当前基线：x86_64 Linux 宿主；首期只保证 ELF64 little-endian、静态归档和
> mcc 生成的 x86_64 汇编/目标文件路径。aarch64 等架构保留目录和接口，
> 不在首期验收范围内。

## 1. 设计原则

### 1.1 工具边界

`meuos-toolchain` 是一个独立项目，最终提供以下无前缀命令：

```text
as       .s -> .o
ld       .o/.a -> ELF 可执行文件或共享对象
ar       .o[] -> .a
nm       符号列表
objdump  反汇编和节区查看
readelf  ELF 结构查看
strip    删除调试/非必要符号
objcopy  节区和格式复制
```

首期只实现 `ar` 和内部 `libelf`；其余目录先建立边界，不放假实现二进制。

### 1.2 内部共享层

```text
include/mt/       项目内部接口，不作为 MeuOS libc 公共头文件安装
src/libelf/       ELF 常量、显式端序读取、结构验证
src/ar/           ar 命令和归档格式读写
src/target/       架构相关编码、重定位和反汇编
```

代码不包含宿主 `<elf.h>`，避免把宿主 libc/GNU 扩展布局带入自举链。所有磁盘
格式读取都通过显式偏移和小端函数完成；后续支持大端或 32 位时增加独立解码路径。

### 1.3 x86_64 首期边界

首期必须支持：

- ELF64 little-endian；`ET_REL`、`ET_EXEC`、`ET_DYN` 头部读取；
- `EM_X86_64`；
- SysV `ar` 短成员名（最多 15 字符）；
- 可复现归档元数据（mtime/uid/gid 固定为 0）；
- mcc 当前生成的 AT&T 汇编子集；
- 静态链接 MeuOS libc 和 `crt1.o`；
- 宿主 Linux 上的 `make check`，以及 QEMU x86_64 上的端到端运行。

首期明确不承诺：完整 gas 兼容、GNU long-name table、archive symbol index、
动态链接器、DWARF、TLS 动态模型和非 x86_64 运行验证。

## 2. 分阶段开发任务

### P0a：基础层与 ar 框架（已完成）

**目标**：建立所有工具共享的编译、错误、ELF 和测试边界，并让短名 `ar rcs` 可用。

P0a 已完成；symbol index/长名等完整归档能力在 P0b 中补齐。

任务：

1. 建立目录、Makefile、依赖文件生成和 `build/` 隔离；
2. 实现 `mt_elf64_parse()`，验证 ELF magic/class/encoding/version 和表范围；
3. 实现 `ar rcs archive member...`、`ar t archive`、`ar p archive`、`ar x archive`；
4. 归档输出使用固定时间、用户、组字段；
5. 为后续 symbol index、长文件名和成员替换预留接口。

验收：

```sh
make -C projects/meuos-toolchain check
```

必须验证：

- `ar --version` 和 `ar --help` 可运行；
- x86_64 relocatable object 可被 `libelf` 识别为 `ET_REL/EM_X86_64`；
- `ar rcs` 创建的归档可列出成员、打印成员、解出成员；
- 解出内容与输入逐字节一致；
- P0a 阶段不要求宿主 `ld` 接受归档；该门禁由 P0b 补齐；
- `git diff --name-only` 只包含本项目文件。

### P0b：ar 完整化（核心完成）

**目标**：让 mt/ar 具备被宿主链接器直接使用的条件。

任务：

1. 解析 ET_REL 的 `.symtab/.strtab`；
2. 生成 GNU `/` 符号索引；
3. 实现 `r/q` 的保留、替换和追加语义；
4. 加入 GNU long-name table；
5. 用宿主 `ld` 链接归档，并跑 mcc `libmcc.a` 构建回归。

验收：

- `cc main.o build/libfoo.a -o main` 成功；
- `ar t`/`nm` 可看到稳定的成员和符号；
- `make -C projects/mcc check` 可在 `MT_AR` 下通过。

### P1：x86_64 汇编器（核心完成，含 SSE/SSE2 标量指令）

任务：

1. 读取 mcc x86_64 emitter 的实际汇编输出，冻结语法子集清单；
2. 实现 lexer、寄存器和立即数解析、标签和节区指令；
3. 实现整数/浮点/控制流/栈操作编码；
4. 生成 ELF64 `ET_REL`，含 `.text/.rodata/.data/.bss/.symtab/.strtab/.rela*`；
5. 支持 crt1.S、atomic.S、setjmp.S 等 MeuOS 运行时汇编；
6. 对每个 instruction family 建立 golden bytes 测试。

当前已完成门禁：

```sh
make -C projects/meuos-toolchain check-as-x86_64 check-as-libc-x86_64
```

- x86_64 mcc 常见整数/内存/栈/分支/call/ret/shift/div/set/cmov 指令可编码；
- `.text/.rodata/.data/.bss` 和常用数据指令可生成；
- 每个 golden case 的字节和 relocation 与参考结果一致；
- 生成的 `.o` 可由宿主 `readelf` 读取；
- mcc 产出的 x86_64 `.s` 和 MeuOS libc 的 crt/atomic/setjmp/sigreturn/thread/syscall 汇编可汇编；
- 错误输入必须报告行号和列号，不得崩溃。

### P2：x86_64 静态链接器（核心完成，含 TLS + counter=2000 验证）

当前核心已通过 `test/ld_smoke.sh`，任务：

1. 读取 ET_REL 和归档成员；
2. 符号解析、强/弱符号、重复定义和未定义符号诊断；
3. 合并节区并按 x86_64 SysV ABI 对齐；
4. 实现 `R_X86_64_32/32S/PC32/PLT32/64/GOTPCREL` 等首期重定位；
5. 输出 ET_EXEC，加入 `PT_LOAD`、入口地址和 `crt1.o` 路径；
6. 处理 `--sysroot`、`-L`、`-l`、`--static`、`-o`。

当前验收门禁：

```sh
make -C projects/meuos-toolchain check-ld-x86_64
```

- mt as + mt ar + mt ld 的 syscall-only ET_EXEC 可在宿主 x86_64 运行；
- 归档输入、GOT/PLT、`.text/.data/.bss`、PC-relative 和绝对 relocation 已覆盖；
- PT_TLS 程序头生成、TPOFF32 TLS 重定位、NOBITS 段布局已修复；
- **`counter = 2000` 多线程程序通过 mt/as + mt/ld 端到端在 QEMU x86_64 上运行通过**；
- SSE/SSE2 标量指令编码与 host as 字节级一致；
- 未定义符号有明确诊断；
- 后续：P3 mcc driver 集成、其他架构 ELF 重定位。

### P3：mcc 集成

只在 P1/P2 完成后修改共享边界文件：

```text
projects/mcc/src/driver/host_toolchain.c
projects/mcc/Makefile
```

约定：

- `MT_AS` 指定 MeuOS `as`；
- `MT_LD` 指定 MeuOS `ld`；
- `MT_AR` 指定 MeuOS `ar`；
- 未设置时保持宿主工具回退，保证 Phase 1 bootstrap 不回归；
- 集成改动单独一个提交，不与架构实现混在一起。

验收：

```sh
make -C projects/mcc check
make -C projects/mcc check-sysroot-static
```

并确认 mcc 的 x86_64 C -> as -> object -> ld -> executable 链路不再调用宿主
`cc` 的汇编/链接路径。

### P4：二进制辅助工具

按依赖顺序实现：

```text
readelf -> nm -> objdump -> strip -> objcopy
```

它们全部复用 `libelf`，先覆盖 x86_64，再扩展其他架构。

验收：输出应能被脚本稳定解析，错误输入必须有非零退出码；`strip`/`objcopy`
不得破坏可执行文件的加载段和必要 relocation。

### P5：自举与回归

1. 用宿主工具构建第一代 mt；
2. 用 mcc + meuos-libc 构建第二代 mt；
3. 用 mt/ar 打包 mcc 的 `libmcc.a`；
4. 用 mt/as + mt/ld 构建 mcc、libc、meow；
5. 在 x86_64 QEMU sysroot 中执行 `bootstrap.sh`；
6. 比较两代工具的行为，不要求 bit-identical。

## 3. 后续架构策略

aarch64 的移植 Agent 负责 mcc 和 meuos-libc 的 ABI/runtime；本项目只读取其
输出契约。mt 的 `src/target/aarch64/` 在 P6 单独实现 ELF relocation/编码，
不直接修改 `projects/mcc/src/target/aarch64/`。

顺序建议：

```text
P6 aarch64 ELF/relocation
P7 riscv64 / loongarch64
P8 i386 ELF32 + time64 运行时链路
P9 动态链接和 TLS
```

每个架构必须同时提供：格式常量、重定位表、golden object、链接运行测试，
不能只添加目录或只通过编译测试。

## 4. 非目标与禁止事项

- 不复制 GNU binutils、gas 或 GNU ld 源码；
- 不依赖宿主 `<elf.h>`、glibc 内部结构或 GNU 专有 API；
- 不把完整 gas 兼容性作为首期目标；
- 不为了通过测试而调用宿主 `as`/`ld` 作为运行时后备；
- 不在 aarch64 Agent 的工作文件中直接修改代码。
