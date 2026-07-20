# 待实现：riscv64 测试环境

## 背景
用户要求 riscv64 后续逐步完善。mcc 已有 riscv64 后端（整数 ABI + 浮点汇编
回归通过），但 QEMU 测试环境尚未覆盖。

## 需要的工作
1. **内核**：Alpine v3.20 的 riscv64 仓库若提供 `linux-virt-6.6.x`，下载 apk
   提取 vmlinuz；否则用 `riscv64-linux-gnu-gcc` 交叉编译 6.6.x 最小内核。
2. **rootfs**：下载 `alpine-minirootfs-3.20.x-riscv64.tar.gz`，扩展
   `bin/build-initramfs.sh` 处理 riscv64 的 9p 模块路径。
3. **QEMU**：在 `build/build-qemu.sh` 的 `--target-list` 增加
   `riscv64-softmmu` 重建。
4. **qvm**：在 `bin/qvm` 增加 riscv64 映射（机器 `virt`，CPU `rv64`，
   控制台 `ttyS0`）。

## 验收
- `qvm boot riscv64` + `qvm run riscv64 'uname -m'` 返回 `riscv64`。
- mcc `--target=riscv64` 产出的静态二进制能在 riscv64 VM 中运行。
