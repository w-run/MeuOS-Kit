<!--
priority: P1
status: pending
note: -O0/-O1/-O2 当前 no-op；阻塞未来优化调度与回归分级
-->

# 待实现：真正的 -O 优化级别控制

## 背景
ARCHITECTURE.md §8 "Still pending"：mcc 当前总是运行全部优化 pass，
`-O0/-O1/-O2` 被接受但不影响代码生成。

## 目标
按 `-O` 级别选择启用/禁用优化 pass：
- `-O0`：仅最小化必需 pass（SSA 构造 + 寄存器分配），不做死代码消除/折叠。
- `-O1`：当前默认（cfg + ssa + mem + copy + load + simpl）。
- `-O2`：加入 gvn / gcm / alias 等更激进 pass。

## 影响范围
- `src/driver/main.c`：把 `-O` 级别传入后端。
- `src/irgen/emit.c` 的 `run_passes()`：按级别分派 pass 子集。
- `src/opt/*.c`：部分 pass 需要可单独跳过。

## 验收
- `-O0` 产出的代码体积明显大于 `-O2`。
- `make check` / `make check-c11` 在各级别下均通过。
