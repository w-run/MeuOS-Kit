# cross-test — 跨架构快速测试 skill
#
# 用法:
#   /cross-test --arch=<arch> <file>          # 自动检测 .c/.o  
#   /cross-test --arch=loongarch64 test.c      # 编译 + QEMU 运行
#   /cross-test --arch=riscv64 test.o          # 直接 QEMU 运行 .o 或 ELF
#   /cross-test --arch=aarch64 -- <args>       # 传递额外参数给 QEMU
#
# 支持架构: aarch64, riscv64, loongarch64, x86_64, i386
# 依赖: mcc (编译器), env/qemu/qemu-<arch> (QEMU 10.1.0)

## 执行流程

1. 解析参数：提取 `--arch` 和文件路径
2. 检查架构是否受支持（`arch_map` 映射表）
3. 如果是 `.c`/`.cpp` 文件，调用 `mcc --target=<arch>` 编译
4. 通过对应架构的 `env/qemu/qemu-<arch>` 运行
5. 显示输出和退出码

## 映射表

```
arch_map:
  aarch64:    { qemu: env/qemu/qemu-aarch64, mcc_target: aarch64 }
  riscv64:    { qemu: env/qemu/qemu-riscv64, mcc_target: riscv64 }
  loongarch64: { qemu: env/qemu/qemu-loongarch64, mcc_target: loongarch64 }
  x86_64:     { qemu: env/qemu/qemu-x86_64, mcc_target: x86_64 }
  i386:       { qemu: env/qemu/qemu-i386, mcc_target: i386 }
```

## 注意

- 编译时默认链接 `meuos-libc`（通过 `--specs=meuos`）
- 可通过 `--nostdlib` 跳过 libc 链接
- QEMU 版本: 10.1.0（自编译）
