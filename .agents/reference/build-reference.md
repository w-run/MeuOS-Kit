# 构建与测试命令参考（.agents/reference/build-reference.md）

> 从 AGENTS.md §8 下放（2026-08-04）。常用构建/测试/自举命令速查，按需读取。

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
# mcc（默认宿主架构，6 个后端全部内置）
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

# meuos-sysroot（libmsys + mkmsys + msysctl）
make -C projects/meuos-sysroot                # 构建 libmsys.a + mkmsys + msysctl
make -C projects/meuos-sysroot so             # 构建 libmsys.so（Python 绑定用）

# mz - 压缩库
make -C projects/meuos-compress                           # 构建 libmz.a
make -C projects/meuos-compress check                     # 压缩/解压缩轮转测试

# meuos-buildtools（Phase 6）
make -C projects/meuos-buildtools             # 构建 m4/gperf/flex
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

#### meuos-sysroot 测试

```sh
make -C projects/meuos-sysroot check         # 打包+校验+单元测试
make -C projects/meuos-sysroot msys          # 从 MEUOS_SYSROOT 生成 .msys
make -C projects/meuos-sysroot check-msys    # 检查已有 .msys 可读性
```

### 8.4 自举

```sh
./bootstrap.sh                                # Phase 0→1（默认）
./bootstrap.sh --phase 0                      # 仅 Phase 0
./bootstrap.sh --phase 2                      # Phase 0→2
./bootstrap.sh --phase 5                      # Phase 0→5 全流程
```

Phase 4 自举验证已通过（`check-sysroot-static`）：sysroot 内 mcc + meow 重新编译全套工具（82 个 .c + libmcc.a + mcc 链接），产物功能等价验证通过。

Phase 5 工具链完善已完成（mt/as + mt/ld 集成到 mcc driver，`check-mt-integration` 验证通过），Kit 全程零宿主依赖已验证。

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
make -C projects/meuos-sysroot clean
```

### 8.7 测试调试指引

当 `make check` 或回归测试失败时，按以下路径排查：

**编译错误 → 常见原因：**
- **缺少 sysroot**：确认 `MEUOS_SYSROOT` 已设置（须指向 `sysroot/<arch>`，如 `sysroot/x86_64`），`$MEUOS_SYSROOT/usr/include` 存在
- **mcc 自身编译失败**：先用 `make -C projects/mcc && make -C projects/mcc check` 确认基础门禁通过
- **交叉工具链缺失**：检查对应架构的 gcc 交叉编译器是否存在（`aarch64-linux-gnu-gcc --version` 等）
- **引用未实现符号**：检查 `.issues/` 排查是否依赖了未实现的功能

**链接错误 → 常见原因：**
- **-l\<lib\> 顺序错误**：mcc 的链接器要求库按依赖顺序排列（引用者在被引用者之前）
- **MT_AS/MT_LD 集成问题**：用 `mcc -v` 查看实际调用的汇编/链接命令，确认走的是 mt 工具链
- **crt1.o 找不到**：确认 `$MEUOS_SYSROOT/usr/lib/crt1.o` 存在

**运行时崩溃 → 快速诊断：**
- **单步调试**：mcc 生成的可执行文件可用宿主 `gdb` 调试（静态链接，含 `-g` 调试信息）
- **qemu-user 运行时**：设置 `QEMU_LD_PREFIX=$MEUOS_SYSROOT` 避免动态库找不到
- **strace**：`strace -o /tmp/trace.log ./a.out` 定位系统调用级问题
- **回归比对**：用 gcc 编译相同源码，比对行为确认是 Kit 问题还是测试用例问题

**测试框架问题：**
- **跳过已知阻塞项**：某些测试依赖未实现特性（如 mcc i386 后端缺口会阻塞 libc i386 TLS 测试），检查 TODO 确认是否为已知阻塞
- **golden bytes 不匹配**：汇编测试（check-as-sse-x86_64 等）的 .expect 文件需要对应架构编码规则更新

---

