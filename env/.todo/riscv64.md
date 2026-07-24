# riscv64 测试环境

## 2026-07-24 状态

### 已完成
- [x] `env/qemu/qemu-riscv64-static` — qemu-user 已下载（v7.2.0）
- [x] `env/build/qemu-install/bin/qemu-system-riscv64` — qemu-system 已自建（v10.1.0）
- [x] `env/kernels/riscv64/vmlinuz` — Linux 6.6.142 内核已交叉编译（22MB），OpenSBI 引导 OK
- [x] `env/rootfs/initramfs-riscv64.cpio.gz` — Alpine 最小 rootfs 已构建
- [x] `env/bin/qvm` — 已添加 riscv64 支持，VM 引导到 shell 验证通过
- [x] `env/bin/build-initramfs.sh` — 已添加 riscv64 分支
- [x] `projects/sysroot-riscv64` — 已安装（4个完整文件）
- [x] `projects/meuos-libc/test/riscv64-bootstrap.sh` — ELF + 非线程运行时通过
- [x] 9p 支持 — 内核内置 `CONFIG_NET_9P` + `CONFIG_9P_FS`

### 已验证
- [x] riscv64 VM 中运行 mcc 产出的静态二进制（qemu-system 已就绪，2026-07-24 验证通过）
  - 修复 crt1.S 缺少 gp 初始化导致 `exit()` segfault（`la gp, __global_pointer$` with `.option norelax`）
  - hello world / return-42 / minimal 全在 qemu-system-riscv64 中通过
