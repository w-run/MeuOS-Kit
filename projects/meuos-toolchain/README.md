# meuos-toolchain (mt)

> MeuOS Kit 的工具链项目：汇编器、链接器、归档器、二进制工具整套提供。
> 解除 mcc 对宿主 `cc`/`as`/`ld`/`ar` 的最后依赖，完成 Kit 自举链。

## 项目定位

**单项目整套提供**：汇编器、链接器、归档器、二进制工具全部在
`projects/meuos-toolchain/` 一个项目里，一次 `make` 构建全套。

- **二进制无前缀**：MeuOS 环境里这些是唯一工具，不需要 `m-` 前缀
    - `as` - 汇编器
    - `ld` - 链接器
    - `ar` - 归档器
    - `ranlib` - 归档索引生成器
    - `nm` / `objdump` / `readelf` / `strip` / `objcopy` - 二进制工具（binutils）
- **内部共享**：`src/libelf/`（ELF 解析库，编译为 `build/lib/libelf.a`）
- **代码不包含宿主 `<elf.h>`**：所有 ELF 常量和结构体自带，避免 GNU 依赖

## 当前状态（base-layer...ld-x86_64 已完成）

| 阶段 | 状态 | 内容 |
|------|------|------|
| P0a | ✅ | libelf + ar 基础框架 |
| P0b | ✅ | ar 完整化：GNU // long-name、/ symbol index、r/q 语义、ranlib、BSD #1/ |
| as-x86_64 | ✅ | x86_64 汇编器：整数/SSE/SSE2 标量编码、ET_REL 生成、golden bytes 对比 |
| ld-x86_64 | ✅ | x86_64 静态链接器：PT_TLS + TPOFF32、-L/-l/-l:/--sysroot、counter=2000 QEMU 端到端 |
| mcc-mt-integrate...target-riscv64 | 规划中 | 见 [ARCHITECTURE.md](ARCHITECTURE.md) |

## 目录结构

```
projects/meuos-toolchain/
├── Makefile                    # 一次构建所有工具
├── ARCHITECTURE.md             # 分阶段架构、任务与验收门禁
├── include/mt/                 # 项目内部头文件
│   ├── archive.h                # ar 接口
│   ├── as.h                     # as 接口
│   ├── elf.h                    # ELF 常量（DT_*, SHT_*, R_X86_64_* 等）
│   └── ld.h                     # ld 接口
├── src/
│   ├── libelf/elf.c            # 内部 ELF 解析库
│   ├── ar/{archive.c,main.c}   # 归档器
│   ├── as/{assemble.c,main.c}  # x86_64 汇编器
│   ├── ld/{link.c,main.c}      # x86_64 静态链接器
│   ├── ranlib/main.c           # 归档索引生成器
│   └── target/<arch>/README.md # 架构占位（target-i386...target-riscv64 实现）
├── test/                       # 测试脚本
│   ├── as_sse_x86_64.sh        # SSE golden bytes 对比
│   ├── as_libc_x86_64.sh       # libc 运行时汇编 fixture
│   ├── ld_smoke.sh             # 链接器基本测试
│   ├── ld_sysroot.sh           # sysroot 链接测试
│   ├── ld_counter_e2e.sh       # counter=2000 端到端（含 QEMU）
│   ├── ld_libpath.sh           # -L/-l/--sysroot/-l: 测试
│   ├── ar_bsd_format.sh        # BSD #1/ 格式测试
│   └── ranlib_basic.sh         # ranlib 测试
└── build/                      # 编译产物（gitignored）
    ├── bin/{ar,as,ld,ranlib}
    └── lib/libelf.a
```

## 构建和验收

```sh
make -C projects/meuos-toolchain clean
make -C projects/meuos-toolchain
make -C projects/meuos-toolchain check
```

`make check` 运行 10 项测试：

```
mt as x86_64 check: PASS          mt ld counter e2e: PASS
mt as SSE x86_64 check: PASS       mt ar BSD format: PASS
mt ld x86_64 smoke: PASS          mt ld libpath: PASS
mt ld x86_64 sysroot smoke: PASS  mt ranlib: PASS
mt check (ar 完整功能): PASS
```

## 工具能力

### ar + ranlib

- `ar rcs/t/p/x` 操作
- GNU `//` long-name table + `/` symbol index
- BSD `#1/` extended-name 格式读取
- `ranlib` 独立生成/更新 symbol index
- 与宿主 `ld` 互操作

### as（x86_64 汇编器）

- mcc 生成的 AT&T 汇编子集（整数 + SSE/SSE2 标量）
- ELF64 `ET_REL` 输出（`.text/.rodata/.data/.bss/.symtab/.strtab/.rela*`）
- MeuOS libc 运行时汇编（crt1/atomic/setjmp/sigreturn/thread/syscall）
- 编码与宿主 `as` 字节级一致

### ld（x86_64 静态链接器）

- ET_REL + 归档读取、符号解析、强弱符号
- 重定位：`R_X86_64_64/32/32S/PC32/PLT32/GOTPCREL/TPOFF32`
- PT_TLS 程序头生成、TLS 静态模型
- `-L`/`-l`/`-l:`/`--sysroot` 库搜索
- counter=2000 多线程程序端到端在 QEMU x86_64 运行通过

## 后续路线图

mcc-mt-integrate...target-riscv64 详见 [ARCHITECTURE.md](ARCHITECTURE.md)：

| 阶段 | 内容 | 规模 |
|------|------|------|
| mcc-mt-integrate | mcc driver 集成（消除宿主 cc 依赖） | S |
| binutils | nm/readelf/objdump/strip/objcopy | M |
| bootstrap-verify | 自举验证（QEMU sysroot 自重建） | M |
| ld-shared | 动态链接（.so + ld.so + dlopen） | L |
| ld-tls-dynamic | TLS 动态模型（GD/LD） | M |
| ld-dwarf | DWARF 调试信息（-g） | M |
| target-i386 | i386 架构 | M |
| target-aarch64 | aarch64 架构 | L |
| target-riscv64 | riscv64 架构 | L |

## 参考实现（许可证友好）

- **musl** `src/internal/elf.h` - ELF 常量定义（MIT）
- **serenityOS** LibELF - ELF 解析（BSD）
- **LLD** - 链接器架构设计参考（Apache 2.0）
- **NASM/YASM** - x86 汇编器参考（BSD）
- 不参考：GNU binutils/gas/ld（GPL）
