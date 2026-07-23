<!--
priority: P3
status: done
done_ts: 2026-07-23
note: 已并入 gd-tls.md（更详细的 5 缺口分解），本文件留档
-->

# 待实现：General-Dynamic TLS 地址模型

## 背景
TARGETS.md §TLS 与共享库边界：x86_64/aarch64/riscv64/loongarch64 都有
local-exec TLS 地址生成回归；x86_64 也覆盖了外部 TLS 的 initial-exec GOT
地址模型。当前尚未实现 general-dynamic TLS；其余 target 的外部 TLS 地址
及含 TLS 的 DSO 仍不在支持范围内。

## 目标
为各后端实现 general-dynamic (GD) TLS 地址模型，支持 DSO 中的外部 TLS
符号解析（`__tls_get_addr` 调用路径）。

## 影响范围
- `src/target/{x86_64,aarch64,riscv64,loongarch64}/*_isel.c`：GD TLS 地址
  指令序列。
- `src/irgen/expr.c` / `src/sema/targ.c`：TLS 模型选择逻辑。
- `src/driver/host_toolchain.c`：链接时传递 TLS 相关运行时符号。

## 前置依赖
- meuos-libc 需要提供 `__tls_get_addr` 实现（或确认 ldso 提供）。

## 验收
- 在 `--shared` 产物中访问外部 `_Thread_local` 变量得到正确值。
- 现有 local-exec / initial-exec 回归不退化。
