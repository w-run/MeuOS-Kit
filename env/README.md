# MeuOS Kit QEMU 测试环境

> 最小化多架构 QEMU 测试环境：基于 Alpine 基础设施 + Linux 6.6.x 内核。
> 用于验证 mcc 编译 + libc-meuos 链接的二进制在真实内核上跨架构运行。

## 支持的架构

| 架构 | QEMU 二进制 | 机器类型 | 控制台 |
|------|------------|---------|--------|
| x86_64 | qemu-system-x86_64 | pc | ttyS0 |
| i386   | qemu-system-i386   | pc | ttyS0 |
| aarch64| qemu-system-aarch64 | virt (cortex-a72) | ttyAMA0 |

`loongarch64` / `riscv64` 待后续完善（见 `src/` 下各项目的 `.todo`）。

## 目录结构

```
env/
├── bin/
│   ├── qvm                 # VM 管理器（启动/连接/停止，替代 qdt 等工具）
│   └── build-initramfs.sh  # 重建 initramfs（Alpine minirootfs + 9p 模块）
├── kernels/<arch>/          # Alpine linux-virt 6.6.142 内核 (vmlinuz + config)
├── rootfs/
│   ├── minirootfs-<arch>.tar.gz   # Alpine 最小根文件系统（busybox+musl+apk）
│   └── initramfs-<arch>.cpio.gz   # 构建好的启动镜像
├── share/                  # 宿主共享目录（9p 挂载到 guest 的 /mnt/host）
├── build/                  # qemu 源码构建（gitignored）
│   ├── qemu-10.1.0.tar.xz
│   ├── qemu-10.1.0/
│   ├── build-qemu.sh       # 构建 qemu（3 targets + 9p）
│   └── qemu-install/bin/   # qemu-system-{x86_64,i386,aarch64}
├── run/                     # 运行时（pid/sock，gitignored）
└── README.md
```

## 设计要点

- **最小化**：initramfs 仅含 Alpine minirootfs（busybox + musl，无多余组件）+ 5 个内核
  模块（netfs/fscache/9pnet/9p/9pnet_virtio）。镜像 3.4–4.0MB。
- **内核 6.6.x**：使用 Alpine `linux-virt-6.6.142` 预编译内核（LTS，VM 优化配置）。
  virtio_pci / virtio_console / devtmpfs 内建；virtio_blk / ext4 / 9p 为模块，
  由 initramfs 的 /init 加载。
- **宿主共享**：9p（virtio-9p）把宿主 `share/` 挂载到 guest `/mnt/host`，
  用于在宿主用 mcc 构建二进制后直接在 guest 运行。RHEL 自带 qemu-kvm 不含 9p，
  故统一使用自建的 qemu。
- **连接方式**：每个 VM 的串口暴露为 UNIX socket，`qvm console <arch>` 用 socat 接入。

## 快速开始

```bash
# 1. 构建 qemu（一次性，~15-20 min）
cd env/build && bash build-qemu.sh

# 2. 构建 initramfs（一次性，几秒）
bash env/bin/build-initramfs.sh

# 3. 启动并连接一个架构
env/bin/qvm boot x86_64
env/bin/qvm console x86_64    # Ctrl-a x 退出
# 在 guest 内：
#   ls /mnt/host               # 宿主共享目录
#   uname -r                   # 6.6.142-0-virt

# 4. 运行测试二进制（宿主用 mcc 构建后放入 share/）
cp projects/mcc/mcc env/share/
env/bin/qvm run x86_64 /mnt/host/mcc --version

# 5. 管理
env/bin/qvm status
env/bin/qvm stop x86_64
```

## 文件传输

宿主 `env/share/` ↔ guest `/mnt/host`（9p，读写）。把待测二进制放入 `share/`，
在 guest 中从 `/mnt/host/` 运行。静态链接的 libc-meuos 二进制无需任何 guest 库。
