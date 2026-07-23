# meuos-toolchain 架构与开发路线图

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

当前已实现 `ar`、`ranlib`、`as`、`ld`、`nm`、`readelf`、`objdump`、`strip`、`objcopy`
和内部 `libelf`、`libdisasm`。P3（mcc driver 集成 MT_AS/MT_LD/MT_AR）与 P4（二进制
辅助工具）已完成。

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

P0-P2 已实现：

- ELF64 little-endian；`ET_REL`、`ET_EXEC`、`ET_DYN` 头部读取；
- `EM_X86_64`；
- SysV `ar` 短成员名、GNU `//` long-name、BSD `#1/` extended-name；
- 可复现归档元数据（mtime/uid/gid 固定为 0）；
- mcc 当前生成的 AT&T 汇编子集；
- 静态链接 MeuOS libc 和 `crt1.o`；
- 宿主 Linux 上的 `make check`，以及 QEMU x86_64 上的端到端运行。

P0-P2 范围（已全部完成）：GNU // long-name table、archive symbol index、
SSE/SSE2 标量编码、TLS 静态模型（IE/LE + PT_TLS + TPOFF32）、BSD #1/ 格式读取、
ranlib、-L/-l/--sysroot 库搜索。
P3-P11 覆盖后续全部能力（详见 §2），不预设优先级。

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

**目标**：让 mt/ar 具备被宿主链接器直接使用的条件。（已完成）

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

已完成的任务：

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
- 后续：P3-P11（见下文）。

### P3：mcc driver 集成

> 消除 Kit 自举链的最后一个外部依赖（宿主 `cc` 做汇编/链接）。

**依赖**：P0-P2　**规模**：S

产物：mcc 通过 `MT_AS`/`MT_LD`/`MT_AR` 调用 mt 工具，不再经宿主 `cc`。

验收：
```sh
MT_AS=<mt>/bin/as MT_LD=<mt>/bin/ld MT_AR=<mt>/bin/ar \
    make -C projects/mcc check check-sysroot-static
strace -f mcc --specs=meuos test.c -o test 2>&1 \
    | grep execve | grep -v '/mt/' | grep -E 'cc|as|ld'   # 应为空
```

---

### P4：二进制辅助工具

> 构建真实软件包时 configure/make 会调用 nm/readelf/objdump/strip/objcopy。

**依赖**：P0（libelf）　**规模**：M

产物：`readelf`、`nm`、`objdump`、`strip`、`objcopy`，全部复用 libelf。

| 工具 | 真实场景 |
|------|---------|
| readelf | configure 检测 ELF 属性、交叉编译验证 |
| nm | 符号冲突检查、库依赖分析 |
| objdump | 反汇编排查代码生成问题 |
| strip | 发布前减小二进制体积 |
| objcopy | 提取节区、格式转换 |

验收：
```sh
make -C projects/meuos-toolchain check-tools
readelf -h build/bin/as | grep -q 'Machine:.*x86-64'
nm build/bin/ar | grep -q ' T main'
cp build/bin/as /tmp/as.s && strip /tmp/as.s && /tmp/as.s --version
objdump -d build/bin/ar | grep -q 'endbr64'
```

---

### P5：自举验证

> AGENTS.md Phase 4 核心要求：Kit 在 MeuOS 环境中自我重建。

**依赖**：P3 + P4　**规模**：M

产物：`bootstrap.sh` 全流程通过，QEMU x86_64 sysroot 中 mt 自重建 Kit。

验收：
```sh
MEUOS_SYSROOT=<sysroot> ./bootstrap.sh
grep -q 'Phase 4.*PASS' bootstrap-report.md
```

---

### P6：动态链接

> 真实软件几乎都依赖共享库。没有动态链接，meow 只能构建纯静态二进制。

**依赖**：P3 + P5　**规模**：L

产物：mt/ld `-shared`（`ET_DYN`）、`-pie`/`-no-pie`、动态链接器 `ld.so`、
libc `dlopen`/`dlsym`/`dlclose`。

任务：
1. mcc: `-fPIC` 位置无关代码生成；
2. mt/ld: 共享库输出（`.dynsym`/`.dynstr`/`.hash`/`.plt`/`.got`）；
3. mt/ld: 动态可执行文件链接、`NEEDED` 条目；
4. `ld.so`: 加载 `.so`、符号解析、懒绑定、RELRO；
5. libc: `dlopen`/`dlsym`/`dlclose`。

验收：
```sh
mcc -shared -fPIC -o libfoo.so foo.c
mcc -o app main.c -L. -lfoo
LD_LIBRARY_PATH=. ./app
readelf -d app | grep -q 'NEEDED.*libfoo.so'
meow build busybox    # 动态链接版本
```

---

### P7：TLS 动态模型

> 共享库中的 `_Thread_local` 需要 GD/LD 模型。glibc 的 `errno` 就是
> `_Thread_local`，任何使用 errno 的共享库都会触发。

**依赖**：P6　**规模**：M

产物：TLS general-dynamic 和 local-dynamic 代码序列、重定位、运行时支持。

任务：
1. mt/as: TLS GD/LD 代码序列编码（`__tls_get_addr` 调用）；
2. mt/ld: `R_X86_64_DTPMOD64`/`R_X86_64_DTPOFF64`/`R_X86_64_TPOFF64`；
3. ld.so: `PT_TLS` 处理、`__tls_get_addr` 实现；
4. libc: `__tls_get_addr` 运行时。

验收：
```sh
# 共享库中的 _Thread_local
echo '_Thread_local int tls_var = 42; int get_tls(void){return tls_var;}' > libtls.c
mcc -shared -fPIC -o libtls.so libtls.c
mcc -o tls_test main.c -L. -ltls
LD_LIBRARY_PATH=. ./tls_test     # 输出 42
# 多线程 TLS 隔离
./tls_threads_test               # 每线程独立
```

---

### P8：DWARF 调试信息

> 构建带 `-g` 的软件包、调试崩溃、stack trace 都需要 DWARF。部分 configure
> 脚本会检测调试信息支持。

**依赖**：P3 + P4（objdump 反汇编）　**规模**：M

产物：mcc `-g` 生成 DWARF v5（`.debug_info`/`.debug_line`/`.debug_abbrev`），
mt/ld 合并重定位 `.debug_*` 节，strip 支持 `--strip-debug`。

任务：
1. mcc: IR/emit 阶段生成 DWARF 调试信息段；
2. mt/ld: 合并 `.debug_*`，应用重定位；
3. strip: `--strip-debug` 只删调试节；
4. readelf: `.debug_info` 解析查看。

验收：
```sh
mcc -g --specs=meuos -o test test.c
readelf --debug-dump=info test | grep -q 'DW_TAG_subprogram'
strip --strip-debug test && readelf -S test | grep -v '\.debug'
./test                          # 仍正常运行
```

---

### P9：i386 架构

> 32 位 x86 仍在嵌入式/兼容场景中使用。某些遗留软件包只支持 i386。

**依赖**：P3　**规模**：M

产物：mt/as + mt/ld 支持 i386（`EM_386`，ELF32）。

任务：
1. mt/as: i386 指令编码（32 位 GPR、x87 浮点）；
2. mt/ld: i386 重定位（`R_386_32`/`R_386_PC32`/`R_386_GOTPC`/`R_386_TLS_*`）；
3. mt/as: i386 crt1.S/atomic.S 运行时汇编。

验收：
```sh
make -C projects/meuos-toolchain check-as-i386 check-ld-i386
qvm run i386 '/mnt/host/i386_counter_test'
```

---

### P10：aarch64 架构

> ARM 服务器和嵌入式平台。MeuOS Next 可能需要跨架构支持。

**依赖**：P3　**规模**：L

产物：mt/as + mt/ld 支持 aarch64（`EM_AARCH64`）。

任务：
1. mt/as: aarch64 指令编码（ADRP/LDR/STR/B/BL 等）；
2. mt/ld: aarch64 重定位（`R_AARCH64_ADR_PREL_PG_HI21`/`R_AARCH64_CALL26` 等）；
3. mt/as: aarch64 crt/atomic/syscall 运行时汇编。

验收：
```sh
make -C projects/meuos-toolchain check-as-aarch64 check-ld-aarch64
qvm run aarch64 '/mnt/host/aarch64_test'
```

---

### P11：riscv64 架构

> RISC-V 是开放架构，MeuOS Next 可能需要支持。

**依赖**：P3　**规模**：L

产物：mt/as + mt/ld 支持 riscv64（`EM_RISCV`）。

任务：
1. mt/as: riscv64 指令编码（LUI/AUIPC/JAL/JALR 等）；
2. mt/ld: riscv64 重定位（`R_RISCV_HI20`/`R_RISCV_CALL` 等）；
3. mt/as: riscv64 crt/atomic/syscall 运行时汇编。

验收：
```sh
make -C projects/meuos-toolchain check-as-riscv64 check-ld-riscv64
qvm run riscv64 '/mnt/host/riscv64_test'
```

## 3. 架构策略

mt 的多架构支持在各架构阶段（P9-P11）中实现。每个架构的 mt/as 编码和 mt/ld
重定位独立于 mcc 的后端：mcc 生成 AT&T 汇编文本，mt/as 负责编码为机器码。

每个架构必须同时提供：指令编码、重定位类型、golden bytes 对比、QEMU 运行
测试，不能只添加目录或只通过编译测试。

mcc 和 libc 的架构移植由各自项目负责（见 `projects/mcc/` 和
`projects/meuos-libc/PORTING.md`），mt 只读取其输出契约（汇编语法、ABI）。

## 4. 非目标与禁止事项

- 不复制 GNU binutils、gas 或 GNU ld 源码；
- 不依赖宿主 `<elf.h>`、glibc 内部结构或 GNU 专有 API；
- 不把完整 gas 兼容性作为目标（只需 mcc 生成的汇编子集）；
- 不为了通过测试而调用宿主 `as`/`ld` 作为运行时后备；
- 不在 aarch64 Agent 的工作文件中直接修改代码。
