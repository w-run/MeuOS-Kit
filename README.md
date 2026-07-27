# MeuOS Kit

MeuOS Kit 是 MeuOS Next 的完整自举开发工具集：C/C++ 编译器、标准 C 库、
构建系统、底层工具链、核心工具集与 Shell。源码树不包含 GCC、LLVM 或 glibc 代码。

> **项目规约（组件规范 / 自举流程 / 禁止事项）** -> 见 [`AGENTS.md`](AGENTS.md)
> **各子项目状态与路线图** -> 见 `projects/<组件>/ARCHITECTURE.md`

## 目录结构

```
MeuOS-Kit/
├── AGENTS.md               项目规约（harness 自动加载）
├── README.md               本文件
├── bootstrap.sh            Phase 0–6 全流程自举脚本
├── projects/
│   ├── mcc/                C/C++ 编译器（C99+C11+C23；后续 m++ 共享后端）
│   ├── meuos-libc/         标准 C 库（ISO C11 + POSIX；含 compat 兼容层）
│   ├── meow/               构建系统（取代 make + autoconf）
│   ├── meuos-toolchain/    底层工具链（as/ld/ar/ranlib，取缔 binutils）
│   ├── meuos-sysroot/      单文件 sysroot 系统（.msys v2，libmsys + mkmsys + msysctl CLI + Python 绑定）
│   ├── meuos-utils/        核心工具集（coreutils/diffutils/findutils 替代）
│   └── meuos-shell/        Shell 终端（msh，POSIX sh + 可选 bash/zsh 兼容）
├── env/                    QEMU 多架构测试环境（6.6.142 内核 + 9p）
├── pkgs/                   meow 构建配方（dash/bzip2/binutils/...）
├── sysroot/                安装目标根文件系统
└── reference/              cproc/QBE/musl 只读参考源（gitignored）
```

每个组件目录含 `ARCHITECTURE.md`（结构/模块/状态/路线图）与 `.todo/`（待实现项）。

## 核心组件

| 组件 | 目标 | 当前状态 |
|------|------|---------|
| `meuos-libc` | ISO C11 + POSIX 标准实现；compat 层独立归档 | x86_64 完整；aarch64/arm qemu 端到端验证；i386 整数 ABI bootstrap；riscv64/loongarch64 代码落地 |
| `mcc` / `m++` | C99+C11 完整，C23 稳定；后续 C++ 共享后端 | C11 核心 + 6 架构后端（x86_64/aarch64/riscv64/i386/loongarch64/arm） |
| `meow` | 取代 make + autoconf | 原生 YAML + Makefile 兼容 |
| `meuos-toolchain` | 取缔 binutils（as/ld/ar/ranlib/nm/objdump/readelf/strip/objcopy） | P0-P4+P9-P11 完成（6 架构 as+ld + .msys Phase 3）；P6-P8 规划中 |
| `meuos-sysroot` | 单文件 sysroot 系统（.msys 格式，mcc/mt/meow 原生读取） | v2 格式完整：SHA-256 去重/校验、ed25519 签名、Overlay 分层、流式消费、xattr、msysctl CLI（22+ 命令）、Python 绑定；已集成到 mcc/mt/ld |
| `meuos-utils` | coreutils/diffutils/findutils 完整替代 | 待启动 |
| `meuos-shell` (msh) | POSIX sh + 可选 bash 兼容 + zsh 插件/主题 | 待启动 |
| `meuos-buildtools` | m4/bison/flex/gperf（取代 GNU 构建工具） | 待启动 |

## Kit 实现 vs meow 软件包

Kit 直接实现自举链基础设施（libc/编译器/工具链/构建系统/Shell/工具集/构建工具）。
应用级库（GMP/MPFR/MPC/GDBM/zlib/openssl 等）通过 `meow build` 从源码构建，不自己实现。
被取代的工具（make/autoconf/cmake）不实现、不构建。详见 [AGENTS.md §2.8](AGENTS.md)。

## 快速开始

```sh
# 用宿主编译器构建 mcc
make -C projects/mcc

# 构建 libc + meow 并安装到 sysroot
make -C projects/meuos-libc install DESTDIR=$(pwd)/sysroot PREFIX=/usr
make -C projects/meow install DESTDIR=$(pwd)/sysroot PREFIX=/usr

# 构建 mt 工具链（as/ld/ar/ranlib）
make -C projects/meuos-toolchain

# 验证
make -C projects/mcc check                # mcc C11 + 各后端 ABI 回归
make -C projects/meuos-libc check         # host (x86_64) libc 全套回归
make -C projects/meow check
make -C projects/meuos-toolchain check    # 10 项测试
make -C projects/meuos-libc check-aarch64-bootstrap  # aarch64 跨编译 + 可选 qemu 运行时

# 全流程自举
./bootstrap.sh
```

`check-aarch64-bootstrap` 默认只验证 aarch64 ELF64/AArch64 头（hello / atomic /
setjmp / phase2 / bare_tls / malloc_threads 全部交叉编译成功）。设
`MEUOS_AARCH64_RUN=1` 且 `MEUOS_AARCH64_QEMU` 指向 qemu-aarch64-static 时附加
运行时 gate：hello 输出 `aarch64 MeuOS libc`、setjmp 输出 `setjmp ok`、
phase2 输出 `counter = 2000`、bare_tls 输出 `tls main=5 child=9 errno=31/47`、
atomic-test 与 malloc_threads exit 0。

`make -C projects/meuos-libc ARCH=arm` 构建 ARM 32-bit libc，
`test/arm-bootstrap.sh` 提供 qemu-arm 运行时验证（hello/atomic/setjmp/exit=42）。```

## 自举流程（AGENTS.md §3）

```
Phase 0  准备          宿主编译器 + sysroot
Phase 1  诞生 mcc      宿主编译 mcc
Phase 2  诞生 libc     mcc 编译 meuos-libc
Phase 3  诞生 meow     mcc + libc 编译 meow
Phase 4  自举验证      sysroot 内自重建 Kit
Phase 5  工具链完善     mcc driver 集成 mt，消除宿主 cc 依赖
Phase 6  构建工具      构建 m4/bison/flex/gperf
Phase 7  用户空间      构建 meuos-utils + meuos-shell
```

当前状态：

| Phase | 范围 | 验证方式 |
|---|---|---|
| 0–3 | 宿主编译 mcc → mcc 编译 libc + meow → 静态 sysroot | `make check` 全套 |
| 4 | sysroot 内自重建 Kit | `make -C meuos-libc check-aarch64-bootstrap`（aarch64 跨 ISA 端到端） + `env/` QEMU |
| 5 | meuos-toolchain + mcc driver 集成 mt | `make -C meuos-toolchain check` |
| 6 | meuos-buildtools (m4/bison/flex/gperf) | 已规划，待启动 |
| 7 | meuos-utils + meuos-shell | 待启动 |

## 测试环境（env/）

`env/bin/qvm` 管理基于 Alpine + 6.6.142 内核的 QEMU VM（x86_64/i386/aarch64/riscv64/loongarch64），
9p 共享宿主目录到 guest `/mnt/host`：

```sh
env/bin/qvm boot aarch64       # 启动 arm64 VM
env/bin/qvm console aarch64    # 进入控制台
env/bin/qvm run aarch64 'uname -m; cat /mnt/host/<binary>'
env/bin/qvm stop aarch64

# ARM qemu-user 运行时
env/qemu/qemu-arm-static ./a.out
```

详见 [`env/README.md`](env/README.md)。

## 许可

RFL (Run Free Software License) v1.0
