# mein libc `make -j$(nproc)` 并行缺陷：只编 1 个 .o 就 exit 0、无 .a 产出

> 状态：🔄 待修复
> 位置：`projects/meuos-libc/Makefile`（`all` 目标，`-jN`）
> 发现：2026-08-05（libc-worker）

## 现象
`make -j12 all` 只编 `__tls_get_addr.o` 一个文件后 exit 0，`libc-meuos.a` 未生成；
非并行 `make all` 完整成功（383 行日志含 ar + crt1 + compat）。

## 根因线索
GNU Make jobserver / `__tls_get_addr.o` 显式规则（Makefile:90-92）与通用 `%.o` 规则
在并行下短路，make 误判 `$(LIBC_LIB)` 为最新。非并行正常 → 并行调度/依赖图问题。

## 修复方向
1. `rm -rf build && make -j12 all` 复现最小化。
2. 定位哪个先决条件并行下被误判 up-to-date。
3. 试 order-only 依赖 / 调整 `all` 依赖顺序 / 排查双规则冲突。
4. 并行+非并行各碰 `make check`，与 meow `-jN` 一起验证。
> 现规避：本项目 `make check` 用非并行。
