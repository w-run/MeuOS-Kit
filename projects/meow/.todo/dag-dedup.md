<!--
priority: P2
status: in_progress
note: -jN 并行执行的间接依赖重复问题；非阻塞，按需优化
start_ts: 2026-07-24
-->

# 待实现：完整 DAG 去重

## 背景
当前 `-jN` 并行执行直接依赖，但共享间接依赖可能在不同 worker 中被重复执行。

## 目标
扩展并行调度为完整 DAG 去重：在单次构建中，每个目标只执行一次，
即使被多个下游目标依赖。

## 影响范围
- `src/graph.c` 的 `run_target()`：需要共享的"已调度/已完成"表，而非
  每个 fork 子进程独立维护 `done` 标志。
- 可能需要引入轻量 IPC（管道/共享内存）或在父进程统一调度后下发任务。

## 验收
- 构造 `A -> B -> C`、`A -> C` 的依赖图，`C` 的命令只执行一次。
- 现有 `meow-parallel` 回归仍通过。

## 验收标准

<!-- TODO(main session): fill in concrete commands. -->

```
make -C projects/meow check
```

