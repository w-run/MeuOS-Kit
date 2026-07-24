# loongarch64 测试环境

## 2026-07-24 状态

### 已完成
- [x] `env/qemu/qemu-loongarch64-static` — qemu-user 已下载（v7.2.0）
- [x] `env/build/qemu-install/bin/qemu-system-loongarch64` — qemu-system 已自建（v10.1.0）
- [x] `env/kernels/loongarch64/vmlinuz` — Linux 6.6.142 内核已交叉编译（34MB），已禁用 i8042 驱动
- [x] `env/kernels/loongarch64/config` — 内核配置文件
- [x] `env/rootfs/initramfs-loongarch64.cpio.gz` — 最小 rootfs（custom C init + dash shell），166KB
- [x] `env/bin/qvm` — 已添加 loongarch64 支持（含 status 列表）
- [x] `env/bin/build-initramfs.sh` — 已添加 loongarch64 分支
- [x] `projects/meuos-libc/test/loongarch64-bootstrap.sh` — 已注册

### 已验证
- [x] `qvm boot loongarch64` → QEMU 引导到 shell（`uname -a` 输出正确）
- [x] 内核 console: `ttyS0 at MMIO` 正常工作
- [x] 9p 共享：hostshare 挂载到 guest `/mnt/host`（需 `-virtfs` 参数）
- [x] 内核 i8042 已禁用（`# CONFIG_SERIO_I8042 is not set`）

### 待办
- [ ] loong64 libc-meuos.a 完整编译（mcc emit 格式需持续完善）
- [ ] loong64 bootstrap 运行时测试（qemu-user + qemu-system）
