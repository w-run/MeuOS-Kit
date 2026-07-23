<!--
priority: P0
status: pending
note: 完整自举硬阻塞；零 GNU 目标要求 mcc + libc + meld 脱宿主 ld
-->

# 待实现：纯原生链接器（脱离宿主 ld）

## 背景
当前 mcc 的链接通过宿主 `cc`/`ld` 完成（见 mcc/src/driver/host_toolchain.c）。
完整自举要求最终在 MeuOS Next 环境中不依赖 GNU/LLVM 工具链。

## 目标
提供 MeuOS 原生链接器（或 lld 移植/简化版），使 mcc + meuos-libc 能在
不调用宿主 `ld` 的情况下产出可执行文件。

## 影响范围
- 新组件（暂定 `projects/meld/` 或 mcc 内嵌链接路径）。
- mcc `host_toolchain.c` 的链接路径需可切换到原生链接器。
- meuos-libc 的 `check-sysroot-static` 回归需适配原生链接。

## 前置依赖
- MeuOS userspace 自身 shell（见 meow `.todo/native-shell.md`）。

## 验收
- 在 chroot 到 `${MEUOS_SYSROOT}` 时，`mcc -o hello hello.c` 不调用宿主 ld。
- Phase-4 自举验证使用原生链接器全链通过。
