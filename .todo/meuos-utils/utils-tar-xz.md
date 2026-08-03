# utils-tar-xz — tar xz/zstd 透传

> **任务 ID**: `utils-tar-xz`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

为 `tar` 增加 xz/zstd 压缩透传（当前仅 gzip），检测到 `.tar.xz`/`.tar.zst` 自动调用对应解压。

## 验收

1. 创建/解包 `.tar.xz` 与 `.tar.zst`
2. 与 GNU tar 互操作
3. `make check` 新增至少 1 项 PASS
