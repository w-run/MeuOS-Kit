# utils-unzip-create — zip 创建能力

> **任务 ID**: `utils-unzip-create`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `unzip` 增加 zip 创建能力（当前仅解压），使 `zip` 工具可创建 PKZIP 归档。

## 验收

1. 创建 zip 归档可被 `unzip -l`/GNU unzip 读取
2. `make check` 新增至少 1 项 PASS
