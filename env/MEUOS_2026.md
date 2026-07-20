# MeuOS 2026 构建虚拟机（QEMU）交接文档

> 面向 MeuOS 2026（`/workspace/MeuOS`，RPM 发行版重建项目）的 Agent。
> MeuOS Kit（`/workspace/MeuOS-Kit`）为它提供一台 **普通 QEMU 虚拟机**，
> 用于后续在 VM 内跑 RPM 构建（替代/补充 podman 容器方案）。
> 先读 [`../STATE.md`](../STATE.md) 与 [`../AGENTS.md`](../AGENTS.md) 了解 MeuOS Kit。

---

## 1. 背景与问题

MeuOS 2026 的 `builder/scripts/run-vm.sh` 用 QEMU/KVM 虚拟机做构建隔离，
但默认调用宿主的 `/usr/libexec/qemu-kvm`（RockyLinux/RHEL 自带）。该二进制：

- ❌ **禁用了 9p**（`virtio-9p-pci is not a valid device`）--导致 run-vm.sh
  默认的 **9p 共享模式失效**（无法把 U盘工作区共享给 VM 当根）。
- ⚠️ 仅 x86_64（无 aarch64 系统仿真）。

MeuOS Kit 在 `env/` 自建了 **QEMU 10.1.0（KVM + 9p + 三 target）**，
正好修复这两个缺口。

## 2. 提供的 QEMU

位置：`/workspace/MeuOS-Kit/env/build/qemu-install/bin/`

| 二进制 | 能力 |
|--------|------|
| `qemu-system-x86_64` | x86_64（KVM 加速 + TCG 回退）+ 9p |
| `qemu-system-i386` | i386（TCG）+ 9p |
| `qemu-system-aarch64` | aarch64（TCG）+ 9p |

特性：`--enable-kvm --enable-tcg --enable-virtfs`，headless（无 GTK/VNC/SDL）。
`/dev/kvm` 在本机可用 -> x86_64 构建走 KVM（接近原生速度），跨架构走 TCG。

## 3. 如何集成（最简方式）

`run-vm.sh` 已支持 `QEMU_BIN` 环境变量覆盖：
```bash
QEMU_BIN="${QEMU_BIN:-/usr/libexec/qemu-kvm}"
```
所以只需：

```bash
# 9p 共享模式（现在能工作了，把 U盘工作区共享给 VM）
QEMU_BIN=$(./env/bin/qemu-path) /workspace/MeuOS/builder/scripts/run-vm.sh

# 磁盘模式（构建用，KVM 加速）
QEMU_BIN=$(./env/bin/qemu-path) /workspace/MeuOS/builder/scripts/run-vm.sh --disk
```

`env/bin/qemu-path` 输出自建 `qemu-system-x86_64` 路径；带架构参数可取 i386/aarch64。

> 建议在 MeuOS 2026 的 `builder/scripts/` 里把 `QEMU_BIN` 默认指向 env/ 的 qemu
> （或加一行 `export QEMU_BIN=${QEMU_BIN:-/workspace/MeuOS-Kit/env/bin/qemu-path}`），
> 这样 9p 模式默认可用，无需每次手动传。

## 4. 附带工具（可提供）

| 工具 | 位置 | 用途 |
|------|------|------|
| `qvm` | `env/bin/qvm` | 多架构 VM 启动/连接/运行（串口 socket + 9p），用于在 aarch64/i386 VM 里测试 MeuOS 2026 的跨架构 RPM 产物 |
| `build-initramfs.sh` | `env/bin/build-initramfs.sh` | 用 Alpine minirootfs + 9p 模块组装最小 initramfs（MeuOS Kit 测试用；MeuOS 2026 的构建 VM 用自己的内核+initrd，不需要这个） |
| `qemu-path` | `env/bin/qemu-path` | 输出 env/ 自建 qemu 路径（集成用） |

这些是纯 bash 脚本，与 qemu 构建方式解耦，可直接复用。MeuOS 2026 若要测跨架构
RPM（如 aarch64），用 `qvm boot aarch64` + `qvm run aarch64 '<cmd>'` 即可。

## 5. 内核 / initrd / 工作区路径约定

MeuOS 2026 现有布局（`run-vm.sh` / `create-disk.sh` 假定）：
- 内核/initrd：`/mnt/meuos-usb/boot/vmlinuz-6.6.29-meuos`、`initrd-full.img`
  （首次运行从 `meuos/builder:2025b` podman 镜像提取）。
- 工作区：`/mnt/meuos-usb/workspace/`（SPECS/RPMS/SRPMS/BUILD/logs）。
- 磁盘镜像：`/mnt/meuos-usb/vm/meuos-disk.qcow2`（`create-disk.sh` 从 podman 镜像导出）。

env/ 的 qemu 不改变这些路径，只替换 QEMU 二进制。MeuOS 2026 的构建流程
（`build-pkg.sh` / `build-all.sh`）在 VM 内执行的方式不变。

## 6. 验证（确认集成生效）

```bash
# 1. env/ qemu 可用且带 9p
$(./env/bin/qemu-path) --version                       # -> QEMU 10.1.0
$(./env/bin/qemu-path) -device help 2>&1 | grep -i 9p   # -> virtio-9p-pci

# 2. run-vm.sh 9p 模式不再报 "virtio-9p-pci is not a valid device"
QEMU_BIN=$(./env/bin/qemu-path) /workspace/MeuOS/builder/scripts/run-vm.sh
#   预期：VM 启动，9p 把 /mnt/meuos-usb/workspace 挂为根，进入 shell

# 3. KVM 加速生效（非 TCG）
QEMU_BIN=$(./env/bin/qemu-path) /workspace/MeuOS/builder/scripts/run-vm.sh --disk
#   预期：构建速度接近原生（KVM），不再是 TCG 的 ~10x 慢

# 4. 跨架构（可选）
env/bin/qvm boot aarch64 && env/bin/qvm run aarch64 'uname -m'  # -> aarch64
```

## 7. 与 MeuOS Kit 自举的关系（长远）

env/ 的 qemu 目前用**宿主 gcc** 构建（`env/build/build-qemu.sh`）。长远目标
（MeuOS Kit Phase 6，见 `env/QEMU_BOOTSTRAP.md`）是让 qemu 改由 mcc+libc-meuos
自建。但**这不阻塞 MeuOS 2026 使用**--当前宿主-gcc 构建的 qemu 完全可用作
MeuOS 2026 的构建 VM；两个项目在此处是"消费方-提供方"关系，解耦。

## 8. 故障排查

| 现象 | 原因 / 处理 |
|------|------------|
| `qemu-system-x86_64: 未找到` | 未构建：`cd env/build && bash build-qemu.sh` |
| 9p 仍报错 | 确认 `QEMU_BIN` 指向 env/ qemu 而非 `/usr/libexec/qemu-kvm` |
| KVM 不生效（变 TCG） | 检查 `/dev/kvm` 与权限；容器内需 `--device /dev/kvm` 透传 |
| VM 内存不足 | `VM_MEM=2048 ./run-vm.sh`（默认 1536MB） |
