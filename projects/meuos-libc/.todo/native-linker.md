<!--
priority: P0
status: done
done_ts: 2026-07-23
note: 添加 check-native-linker 目标，端到端验证 mt/ld 原生链接器；mt/ld P2 + mcc driver P3 已先前完成
-->

# 待实现：纯原生链接器（脱离宿主 ld）

## 背景
当前 mcc 的链接通过宿主 `cc`/`ld` 完成（见 mcc/src/driver/host_toolchain.c）。
完整自举要求最终在 MeuOS Next 环境中不依赖 GNU/LLVM 工具链。

## 目标
提供 MeuOS 原生链接器（或 lld 移植/简化版），使 mcc + meuos-libc 能在
不调用宿主 `ld` 的情况下产出可执行文件。

## 影响范围
- **mt/ld**（`projects/meuos-toolchain/src/ld/`）已实现 P2 x86_64 静态链接。
- mcc `host_toolchain.c` 的链接路径需可切换到 `mt/ld`（P3 已落 `MT_LD` 环境变量分流）。
- meuos-libc 的 `check-sysroot-static` 回归需适配 `mt/ld` 链接（已用于 sysroot 构建）。

## 前置依赖
- MeuOS userspace 自身 shell（见 meow `.todo/native-shell.md`）。

## 验收
- 在 chroot 到 `${MEUOS_SYSROOT}` 时，`mcc -o hello hello.c` 不调用宿主 ld。
- Phase-4 自举验证使用原生链接器全链通过。
