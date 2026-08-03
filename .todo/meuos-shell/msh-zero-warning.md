# msh-zero-warning — 零警告编译

> **任务 ID**: `msh-zero-warning`
> **范围**: `projects/meuos-shell/src/` 全目录
> **依赖**: 无
> **状态**: ⏳ 待启动

## 目标

`make` 在 `-Wall -Wextra -Wpedantic` 下零警告。

## 现状（2026-08-04 实测）

主要警告源：
- `src/exec/cmd_dispatch.c:56` — `unused parameter 'argc'/'argv'`
- `src/exec/plugin.c:360/371/384/449/497/499/624/629` — `-Wformat-truncation`（snprintf 目标缓冲区大小保守估计）

## 验收

1. `make` 输出零 warning
2. 不改变运行时行为
3. `make check` 全绿
