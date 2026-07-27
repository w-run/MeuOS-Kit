# ARM QEMU 环境

> 2026-07-27 完成：qemu-arm 运行时验证通过（hello/atomic/setjmp/exit=42）。

## 已完成

- **qemu-arm 静态二进制**：`env/qemu/qemu-arm-static`（10.1.0 自建版本）
- **运行时测试**：
  - hello: exit=0 正常输出
  - atomic: C11 原子操作验证通过
  - setjmp: 非局部跳转验证通过
  - exit=42: 退出码验证通过
- **set_tls**：使用 `__kuser_set_tls` 内核辅助（0xffff0fe0），无需特权 `mcr p15` 指令

## 待完成

- **qemu-system-arm VM**：当前只有 qemu-user，没有完整的系统仿真 VM 环境
- **ARM rootfs**：`env/rootfs/` 中 ARM 的 initramfs 镜像
- **ARM 内核**：`env/kernels/arm/` 内核
- **qvm 集成**：`env/bin/qvm arm` 子命令
- **跨架构集成测试**：ARM + aarch64 混合测试门禁
