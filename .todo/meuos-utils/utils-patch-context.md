# utils-patch-context — patch context 格式

> **任务 ID**: `utils-patch-context`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `patch` 增加 context 格式（GNU patch 的 `-c` context diff 格式）支持，当前仅 unified。

## 验收

1. 应用 context 格式 diff
2. `make check` 新增至少 1 项 PASS
