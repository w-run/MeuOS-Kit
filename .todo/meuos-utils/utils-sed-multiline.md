# utils-sed-multiline — sed 多行模式

> **任务 ID**: `utils-sed-multiline`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `sed` 增加多行模式（GNU sed 的 `N`/`D`/`P` 命令与范围地址），当前仅单行处理。

## 验收

1. `N`/`D`/`P` 命令可用
2. 范围地址（`/start/,/end/`）跨行生效
3. `make check` 新增至少 1 项 PASS
