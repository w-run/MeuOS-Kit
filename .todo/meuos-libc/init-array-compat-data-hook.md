# 工具链缺 .init_array 数据钩子 → compat 归档无法挂构造器/跨归档写

> 状态：🔄 待专项
> 发现：2026-08-05（libc-worker，item b/c）

## 背景
此为 `program_invocation_*` 数据被迫落 core（commit 2b80177）的根因工具链缺口。

## 3 个实证限制
1. crt 无 `.init_array`/`__libc_start_main` → opt-in 归档无启动钩子。
2. core 对 undefined-weak 的「写」→ pcrel load from addr 0 → SIGSEGV（工具链不折叠）。
3. GAS `.set` 别名跨 archive 不可链接；GCC `alias` 属性要求同 TU 目标。

## 根治方向
- 给 crt1 + mt/ld 加 `.init_array`/`.fini_array` 支持；或 core 暴露 init 指针表。
- 修复 unresolved-weak → 0 折叠。
- 修好后将 program_invocation_* 数据移回 compat（当前 core 拥有数据、compat
  拥有 program-invocation.h 声明头）。详见 ARCHITECTURE.md §5。
