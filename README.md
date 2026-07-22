# MeuOS Kit

MeuOS Kit 是 MeuOS Next 的自举工具链：`mcc` C11 编译器、`meuos-libc`
C 库、`meow` 构建系统与 `meuos-toolchain` 工具链（汇编器/链接器/归档器）。
源码树不包含 GCC、LLVM 或 glibc 代码。

> **当前状态、阶段进度、下一步优先级、会话恢复指南** → 见 [`STATE.md`](STATE.md)
> **项目规约（组件规范 / 自举流程 / 禁止事项）** → 见 [`AGENTS.md`](AGENTS.md)

## 目录结构

```
MeuOS-Kit/
├── AGENTS.md            项目规约（harness 自动加载）
├── STATE.md             当前状态 + 会话恢复（动态单一事实源）
├── README.md            本文件
├── bootstrap.sh         Phase 0–5 全流程自举脚本
├── projects/            四个核心组件
│   ├── mcc/             C11 编译器（源码级整合 cproc+QBE）
│   ├── meuos-libc/      直接内核 ABI 的 C 库 + compat 兼容层
│   ├── meow/            原生 YAML 构建系统
│   └── meuos-toolchain/ 汇编器 as / 链接器 ld / 归档器 ar / ranlib
├── env/                 QEMU 多架构测试环境（6.6.142 内核 + 9p）
├── pkgs/                meow 构建配方（dash/bzip2/binutils/...）
├── sysroot/             安装目标根文件系统
└── reference/           cproc/QBE/musl 只读参考源（gitignored）
```

每个组件目录含 `ARCHITECTURE.md`（结构/模块索引）与 `.todo/`（待实现项，
每项含背景/目标/影响范围/验收）。`meuos-libc` 另有 [`PORTING.md`](projects/meuos-libc/PORTING.md)，
记录多架构状态、ABI 契约、time64 策略和移植门禁。mcc 额外含 `TARGETS.md`（各架构成熟度）。
`meuos-toolchain` 的分阶段路线图（P0-P11）见
[`ARCHITECTURE.md`](projects/meuos-toolchain/ARCHITECTURE.md)。

## 快速开始

```sh
# 用宿主编译器构建 mcc
make -C projects/mcc

# 构建 libc + meow 并安装到 sysroot
make -C projects/meuos-libc install DESTDIR=$(pwd)/sysroot PREFIX=/usr
make -C projects/meow install DESTDIR=$(pwd)/sysroot PREFIX=/usr

# 构建 mt 工具链（as/ld/ar/ranlib）
make -C projects/meuos-toolchain

# 验证（详见 STATE.md 第 5 节）
make -C projects/mcc check
make -C projects/meuos-libc check
make -C projects/meow check
make -C projects/meuos-toolchain check   # 10 项测试

# 全流程自举
./bootstrap.sh
```

## 自举流程（AGENTS.md §3）

Phase 0 准备 → 1 诞生 mcc → 2 诞生 meuos-libc → 3 诞生 meow → 4 自举验证 →
5 LFS 包验证。当前 Phase 0–3 与 5 均已 PASS，Phase 4 由 `env/` QEMU 6.6.142
内核环境验证 mcc+libc-meuos 二进制可运行。

## 测试环境（env/）

`env/bin/qvm` 管理基于 Alpine + 6.6.142 内核的 QEMU VM（x86_64/i386/aarch64），
9p 共享宿主目录到 guest `/mnt/host`，用于运行 mcc 编译出的跨架构二进制：

```sh
env/bin/qvm boot aarch64       # 启动 arm64 VM
env/bin/qvm console aarch64    # 进入控制台
env/bin/qvm run aarch64 'uname -m; cat /mnt/host/<binary>'
env/bin/qvm stop aarch64
```

详见 [`env/README.md`](env/README.md)。loongarch64/rv64 见 `env/.todo/`。

## 许可

MIT
