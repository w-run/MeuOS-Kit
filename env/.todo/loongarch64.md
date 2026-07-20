# 待实现：loongarch64 测试环境

## 背景
用户要求 loongarch64 后续逐步完善。mcc 已有 loongarch64 后端（专项
ABI/VLA/TLS 汇编回归通过），但 QEMU 测试环境尚未覆盖。

## 需要的工作
1. **内核**：Alpine v3.20 的 loongarch64 仓库若提供 `linux-virt-6.6.x`，
   下载 apk 提取 vmlinuz；否则从 kernel.org 取 6.6.x 源码用
   `loongarch64-linux-gnu-gcc` 交叉编译最小内核。
2. **rootfs**：下载 `alpine-minirootfs-3.20.x-loongarch64.tar.gz`，
   用 `bin/build-initramfs.sh` 增加 loongarch64 分支（提取对应 arch 的 9p 模块）。
3. **QEMU**：当前自建 qemu 未含 `loongarch64-softmmu` target。在
   `build/build-qemu.sh` 的 `--target-list` 增加 `loongarch64-softmmu` 重建。
4. **qvm**：在 `bin/qvm` 增加 loongarch64 映射（机器 `virt`，CPU `la464`，
   控制台 `ttyS0`）。

## 验收
- `qvm boot loongarch64` + `qvm run loongarch64 'uname -m'` 返回 `loongarch64`。
- mcc `--target=loongarch64` 产出的静态二进制能在 loongarch64 VM 中运行。
