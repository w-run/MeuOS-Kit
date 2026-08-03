# utils-awk-varassign — awk 变量赋值 gsub 修复

> **任务 ID**: `utils-awk-varassign`
> **范围**: `projects/meuos-utils/src/utils/`
> **状态**: ⏳ 待启动

## 目标

修复 awk 的变量赋值 gsub（当前 gsub 修改后的值不写回变量，如 `{gsub(/x/,"y",v); print v}`）。

## 验收

1. `gsub` 修改的变量值正确写回
2. `make check` 新增至少 1 项 PASS
