# utils-gzip-lz77 — gzip LZ77 压缩

> **任务 ID**: `utils-gzip-lz77`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `gzip` 增加 LZ77 压缩（当前仅 stored 压缩），提升压缩率至接近 GNU gzip。

## 验收

1. `gzip` 压缩产物可被 GNU gzip 解压
2. 压缩率较 stored 显著提升
3. `make check` 新增至少 1 项 PASS
