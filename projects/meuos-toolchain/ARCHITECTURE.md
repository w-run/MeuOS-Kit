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

首期范围（P0-P2 已全部完成）：GNU // long-name table、archive symbol index、
SSE/SSE2 标量编码、TLS 静态模型（IE/LE + PT_TLS + TPOFF32）、BSD #1/ 格式读取、
ranlib、-L/-l/--sysroot 库搜索。
首期不包含（后续阶段）：完整 gas 兼容、动态链接器、DWARF 调试信息、
TLS 动态模型（GD/LD）、非 x86_64 架构运行验证。

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

### P0b：ar 完整化（完成，含 ranlib + BSD #1/）

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

### P2：x86_64 静态链接器（完成，含 TLS + counter=2000 + -L/-l/--sysroot）

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
- `-L`/`-l`/`-l:`/`--sysroot` 库搜索路径已支持；
- 后续：P3 mcc driver 集成、其他架构 ELF 重定位。

### P3：mcc driver 集成

> **为什么需要**：当前 mcc 通过 `host_toolchain.c` 把汇编/链接外包给宿主 `cc`，
> 这是 Kit 自举链的最后一个外部依赖。消除它才能实现真正的自我重建。

**依赖**：P0-P2（已完成）
**规模**：S

**产物**：mcc 不再调用宿主 `cc`/`as`/`ld`，改用 `mt/as` + `mt/ld` + `mt/ar`。

任务：
1. `projects/mcc/src/driver/host_toolchain.c` 增加 mt 工具调用路径；
2. `projects/mcc/Makefile` 在 `MT_AS`/`MT_LD`/`MT_AR` 设置时使用 mt 工具；
3. 未设置时保持宿主回退（Phase 1 bootstrap 不回归）。

验收：
```sh
MT_AS=<mt>/bin/as MT_LD=<mt>/bin/ld MT_AR=<mt>/bin/ar \
    make -C projects/mcc check check-sysroot-static
# strace 确认 mcc 不调用宿主 cc
MT_AS=<mt>/bin/as MT_LD=<mt>/bin/ld \
    strace -f mcc --specs=meuos test.c -o test 2>&1 \
    | grep 'execve' | grep -v '/mt/' | grep -E 'cc|as|ld'
# 上面的输出应为空
```

---

### P4：二进制辅助工具

> **为什么需要**：构建真实软件包（busybox/binutils/coreutils）时，configure
> 脚本和 Makefile 会调用 `nm`/`readelf`/`objdump`/`strip`/`objcopy`。没有
> 这些工具，meow 无法完整构建任何非平凡软件包。

**依赖**：P0（libelf）
**规模**：M

**产物**：`readelf`、`nm`、`objdump`、`strip`、`objcopy`。

| 工具 | 真实场景 | 复用 |
|------|---------|------|
| readelf | `configure` 检测 ELF 属性、交叉编译验证 | libelf |
| nm | 链接时符号检查、调试符号冲突 | libelf symtab |
| objdump | 反汇编排查代码生成问题 | libelf + 解码器 |
| strip | 发布前减小二进制体积 | libelf 写入 |
| objcopy | 提取 `.text` 做 firmware/boot image | libelf 写入 |

验收：
```sh
make -C projects/meuos-toolchain check-tools
# readelf 输出能被 configure 脚本解析
readelf -h build/bin/as | grep -q 'Machine:.*x86-64'
# nm 输出兼容宿主格式
nm build/bin/ar | grep -q ' T main'
# strip 后程序仍可运行
strip build/bin/as && build/bin/as --version
# objdump -d 反汇编
objdump -d build/bin/ar | grep -q 'endbr64'
```

---

### P5：自举验证

> **为什么需要**：证明 Kit 工具链能在 MeuOS 环境中自我重建。这是 AGENTS.md
> Phase 4 的核心要求，也是"零 GNU 代码"自举链的最终验证。

**依赖**：P3 + P4
**规模**：M

**产物**：`bootstrap.sh` 全流程通过，在 QEMU x86_64 sysroot 中用 mt 工具链
重建 mcc、libc、meow、mt 自身。

任务：
1. Phase 0-1：宿主工具构建第一代 mt；
2. Phase 2-3：mcc + libc-meuos + mt 工具链构建第二代 mt；
3. Phase 4：QEMU sysroot 中执行 `bootstrap.sh`，两代工具行为对比；
4. 输出 `bootstrap-report.md`。

验收：
```sh
MEUOS_SYSROOT=<sysroot> ./bootstrap.sh
grep -q 'Phase 4.*PASS' bootstrap-report.md
# 两代 mcc 编译同一程序，行为等价（不要求 bit-identical）
gen1_mcc -S test.c -o gen1.s && gen2_mcc -S test.c -o gen2.s
gen1_mcc test.c -o gen1 && gen2_mcc test.c -o gen2
diff <(./gen1) <(./gen2)  # 输出一致
```

---

### P6：动态链接

> **为什么需要**：真实系统中几乎所有软件都依赖共享库（libc.so、libssl.so
> 等）。没有动态链接，meow 只能构建纯静态二进制，无法构建真实发行版。
> 这是 P3-P5 之后最关键的基础设施缺口。

**依赖**：P3 + P5（自举验证通过）
**规模**：L（最大单项工程）

**产物**：mt/ld 支持 `-shared` 生成 `ET_DYN` 共享库，动态链接器 `ld.so`，
mcc 支持 `-fPIC`/`-shared`，libc 提供 `dlopen`/`dlsym`。

任务：
1. mcc: `-fPIC` 生成位置无关代码（GOTPCREL/PLT 已有基础）；
2. mt/ld: `-shared` 生成共享库（`.dynsym`/`.dynstr`/`.hash`/`.plt`/`.got`）；
3. mt/ld: `-pie`/`-no-pie` 选项，动态可执行文件链接；
4. `ld.so`: 动态链接器（加载 `.so`、符号解析、懒绑定、RELRO）；
5. libc: `dlopen`/`dlsym`/`dlclose`。

验收：
```sh
# 构建共享库
mcc -shared -fPIC -o libfoo.so foo.c
# 动态链接
mcc -o app main.c -L. -lfoo
# 运行
LD_LIBRARY_PATH=. ./app
# 依赖关系正确
readelf -d app | grep -q 'NEEDED.*libfoo.so'
# busybox 动态构建
meow build busybox  # 动态链接版本
```

---

### 后续（按需，不预设阶段）

以下能力在 P6 完成后按 MeuOS Next 实际需求安排，不预设优先级：

- **多架构**（i386/aarch64/riscv64）：取决于 MeuOS Next 目标平台
- **DWARF 调试信息**：取决于是否有 gdb 或其他调试器
- **TLS 动态模型**（GD/LD）：取决于共享库中是否使用 `_Thread_local`
- **完整 gas 兼容**：只需 mcc 生成的子集，非目标

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
