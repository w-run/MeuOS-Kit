# utils-tail-follow — `tail -f` 跟随模式

> **任务 ID**: `utils-tail-follow`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `tail` 增加 `-f`（follow）模式：文件增长时持续输出新追加内容（GNU tail -f 兼容）。

## 验收

1. `tail -f` 在文件追加内容时持续输出
2. `--classic` 兼容行为一致
3. `make check` 新增至少 1 项 PASS
