# loongarch64 测试环境

## 2026-07-24 状态

### 已完成
- [x] `env/qemu/qemu-loongarch64-static` — qemu-user 已下载（v7.2.0）
- [x] `env/build/qemu-install/bin/qemu-system-loongarch64` — qemu-system 已自建（v10.1.0）
- [x] `env/kernels/loongarch64/vmlinuz` — Linux 6.6.142 内核已交叉编译（34MB）
- [x] `env/bin/qvm` — 已添加 loongarch64 支持
- [x] `projects/sysroot-loongarch64` — crt1.o + libatomic-meuos.a + 头文件已安装
- [x] `projects/meuos-libc/test/loongarch64-bootstrap.sh` — 已注册

### 待实现
- [ ] 内核 i8042 配置修复：在 defconfig 基础上禁用 `CONFIG_SERIO_I8042` 并重建
- [ ] 构建 initramfs（LoongArch 无 Alpine 端口，需用 busybox 静态编译或 buildroot 构建最小 rootfs）
- [ ] 修复后验证 `qvm boot loongarch64` 引导到 shel
- [ ] loong64 libc-meuos.a 完整编译（mcc emit 格式需持续完善）
- [ ] loong64 bootstrap 运行时测试（qemu-user + qemu-system）
