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
| `meuos-libc` | ISO C11 + POSIX 标准实现；compat 层独立归档 | x86_64 完整，i386 可用 |
| `mcc` / `m++` | C99+C11 完整，C23 稳定；后续 C++ 共享后端 | C11 核心 + 多架构 |
| `meow` | 取代 make + autoconf | 原生 YAML + Makefile 兼容 |
| `meuos-toolchain` | 取缔 binutils（as/ld/ar/ranlib/nm/objdump/readelf/strip/objcopy） | P0-P2 完成，P3-P11 规划中 |
| `meuos-utils` | coreutils/diffutils/findutils 完整替代 | 待启动 |
| `meuos-shell` (msh) | POSIX sh + 可选 bash 兼容 + zsh 插件/主题 | 待启动 |

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
make -C projects/mcc check
make -C projects/meuos-libc check
make -C projects/meow check
make -C projects/meuos-toolchain check   # 10 项测试

# 全流程自举
./bootstrap.sh
```

## 自举流程（AGENTS.md §3）

```
Phase 0  准备          宿主编译器 + sysroot
Phase 1  诞生 mcc      宿主编译 mcc
Phase 2  诞生 libc     mcc 编译 meuos-libc
Phase 3  诞生 meow     mcc + libc 编译 meow
Phase 4  自举验证      sysroot 内自重建 Kit
Phase 5  工具链完善     mcc driver 集成 mt，消除宿主 cc 依赖
Phase 6  用户空间      构建 meuos-utils + meuos-shell
```

当前 Phase 0–3 与 5（LFS 包验证）均已 PASS。Phase 4 由 `env/` QEMU 验证。
Phase 5-6 进行中。

## 测试环境（env/）

`env/bin/qvm` 管理基于 Alpine + 6.6.142 内核的 QEMU VM（x86_64/i386/aarch64），
9p 共享宿主目录到 guest `/mnt/host`：

```sh
env/bin/qvm boot aarch64       # 启动 arm64 VM
env/bin/qvm console aarch64    # 进入控制台
env/bin/qvm run aarch64 'uname -m; cat /mnt/host/<binary>'
env/bin/qvm stop aarch64
```

详见 [`env/README.md`](env/README.md)。

## 许可

MIT
