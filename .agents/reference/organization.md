# 项目组织参考（.agents/reference/organization.md）

> 从 AGENTS.md §5/§11 下放（2026-08-04）。目录结构、构建约定、QEMU 环境、Issue/TODO 导航。

## 5. 项目组织

### 5.1 目录结构

```
MeuOS-Kit/
├── AGENTS.md               项目规约（本文件，harness 自动加载）
├── README.md               项目说明与构建方法
├── bootstrap.sh            Phase 0–5 全流程自举脚本
├── cron.md                 循环任务定义（session 级，随会话结束清理）
├── issue/                  全局 issue 追踪（按日期命名的详细待实现/缺陷分析清单）
├── .issues/                当前 worktree 的任务队列与入口文档（worktree 活跃期间有效）
├── projects/
│   ├── mcc/                C/C++ 编译器（C99+C11+C23；m++ 待启动）
│   ├── meuos-libc/         标准 C 库（ISO C11 + POSIX；含 compat 兼容层）
│   ├── meow/               构建系统（取代 make + autoconf）
│   ├── meuos-toolchain/    底层工具链（as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump）
│   ├── meuos-sysroot/      .msys 单文件 sysroot 系统（libmsys + mkmsys + msysctl CLI + Python 绑定，已集成到 mcc）
│   ├── meuos-utils/        核心工具集（骨架：libutils.a + 5 烟雾工具）
│   ├── meuos-shell/        Shell 终端（骨架：-c/script/REPL 三模式）
│   ├── meuos-buildtools/   构建工具（m4/gperf/flex/bison）
│   └── meuos-compress/       压缩库（libmz.a，LZ77）
├── env/                    QEMU 多架构测试环境（6.6.142 内核 + 9p 共享）
│   ├── bin/qvm             VM 管理器
│   ├── qemu/               静态 qemu-user 二进制（aarch64/riscv64/loongarch64）
│   ├── kernels/<arch>/     Alpine linux-virt 6.6.142 内核
│   └── rootfs/             initramfs 镜像
├── pkgs/                   meow 构建配方（YAML；dash/bzip2/binutils 等）
├── sysroot/                安装目标根文件系统（默认 MEUOS_SYSROOT）
└── reference/              cproc/QBE/musl 只读参考源（gitignored）
```

每个组件目录含 `ARCHITECTURE.md`（结构/模块/状态/路线图）。待办事项统一存放在 `.issues/` 下。

**配方包（pkgs/）**：`pkgs/` 存放 `.meow` 格式构建配方，涵盖基础依赖库（dash/bzip2/binutils）、meow 自测试配方（`meow-smoke`、`meow-incremental` 等）和 Kit 组件配方（`mcc`、`meow`、`meuos-libc`）。通过 `meow build <pkg>` 使用，详见 `pkgs/<pkg>/project.meow`。

**sysroot 多架构布局**：`sysroot/` 下按架构分目录，支持多架构同时安装：
```
sysroot/
├── x86_64/        # 默认架构（ARCH= 缺省值）
├── aarch64/
├── arm/
├── i386/
├── loongarch64/
└── riscv64/
```
跨架构安装时指定 `ARCH=<arch>`，如 `make -C projects/meuos-libc ARCH=aarch64 install`。

### 5.2 构建约定

- 每个组件用**简单 Makefile** 构建（§4 禁止 autotools/cmake/meson）。
- 编译产物放入独立输出目录（通常是 `build/`），不污染源码树。
- `MEUOS_SYSROOT` 环境变量控制安装目标路径（默认 `<repo-root>/sysroot`）。
- 跨架构时 `ARCH=<arch>` 切换目标（x86_64/aarch64/riscv64/loongarch64/i386）。
- 每个 `.S` 文件通过宿主 cc 或交叉 gcc 汇编（mcc 不处理内联汇编指令）。

### 5.3 QEMU 测试环境

`env/` 提供基于 Alpine Linux 6.6.142 内核的 QEMU 测试环境，支持完整系统仿真和
单 ELF 运行时验证：

- **qemu-system VM**（完整系统仿真）：x86_64 / i386 / aarch64 / riscv64 / loongarch64
- **qemu-user**（单 ELF 运行时验证）：aarch64 / riscv64 / loongarch64 静态二进制可用
- **qvm 管理器**（`env/bin/qvm`）：`qvm boot|console|run|stop <arch>`
- **9p 共享**：宿主 `share/` 挂载到 guest 的 `/mnt/host`

详见 `env/README.md` 和 §8.1 命令。

---

## 11. Issue/TODO 导航系统
