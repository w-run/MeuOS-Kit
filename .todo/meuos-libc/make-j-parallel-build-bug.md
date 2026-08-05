# meuos-libc `make -j$(nproc)` 并行构建缺陷：只编 1 个 .o 就 exit 0、无 .a 产出

> 状态：✅ 已解决（根因已由 `16114170` 修复，2026-08-02）
> 复核：2026-08-06（libc-worker，串行 + -j$(nproc) 双向回归确认）
> 位置：`projects/meuos-libc/Makefile`

## 根因（已定位实锤）

裸 `make`（含 `make -jN`）在修复前**没有默认目标**。x86_64 分支在 Makefile
前部（`all:` 之前）显式定义了 `$(BUILD)/thread/__tls_get_addr.o` 规则，GNU
make 于是把**文件内第一个显式目标** `__tls_get_addr.o` 当成默认目标 → 只编
译该一个对象即 exit 0，`$(LIBC_LIB)` 从未构建，无 .a 产出。

**实证**（用修复前提交 `da205fab` 建 fresh worktree）：
- 裸 `make -j12`：`exit 0`、`.a=NO`、只编译 `build/x86_64/thread/__tls_get_addr.o` 一个对象——与症状完全一致。
- 同提交显式 `make all`：`exit 0`、`.a=yes`、编译 131 对象——与「make all 正常」一致。

## 修复（已存在于 main，commit `16114170`）

```make
.DEFAULT_GOAL := all
```
显式固定默认目标为 `all`，使任何规则位置/顺序都无法再抢占默认目标。这结构性
解决"前部显式规则抢先成为默认目标"的陷阱，`-jN` 与串行一致。

另 `.PHONY: all compat install check ...`（Makefile:709）完整声明了 phony 目标。

## 复核（当前 main = fbb79474）

| 场景 | 结果 |
|------|------|
| 裸 `make -j12`（fresh worktree） | exit 0、编译 165 对象、`.a=yes` |
| `make -j12 all`（多次迭代） | exit 0、`.a=yes` |
| 串行 `make check` | exit 0、84 PASS、无回归 |
| `make -j12 check` | exit 0、84 PASS、无回归（与串行逐条一致）|

结论：无需额外代码改动（修复已在 main），本缺陷关闭。`make check` 已可任意
并行执行，"非并行规避"备注同步作废。
