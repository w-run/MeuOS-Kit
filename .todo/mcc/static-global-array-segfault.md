# 静态 exe + 全局数组运行 segfault（x86_64 R_X86_64_PC32/绝对数组寻址）

> 状态：✅ 已闭环（2026-08-07）
> 关联 commit：dfcc0dc7 / 9e65b72d / 9f1cf2be

## 现象

- **静态 exe + 全局数组**（如 `int gdata[4] = {...}`），**无论有无 TLS**，运行 **segfault(139)**；
- 崩溃点：crt `.fini` / `.init_array` 阶段，或 main 内数组访问；
- exec-toolchain-d5 在验证方案 B 时触发，但**与 TLS/方案 B 无关**。

## 判定

- **非 TLS / 方案 B 引入**：基线 `07303b2` **亦复现**；
- 既有 bug，定位指向 **mcc / mt 对 `R_X86_64_PC32` / 绝对数组寻址的组合**；
- 属 mcc-toolchain 既有缺陷，独立于 GD TLS P1 链路。

## 范围

- **mcc x86_64 后端**：全局数组**绝对寻址**（数组符号引用生成），排查 PC-relative / absolute reloc 生成路径；
- **mt/ld**：`R_X86_64_PC32` 重定位处理（静态可执行 + 数组符号重定位）；
- 崩溃点在 crt 阶段或 main 数组访问，需结合 crt `.init_array`/`.fini` 一并排查。

## 验收

- 静态 exe + 全局数组（`int gdata[4]={...}`）运行正常，数组访问/初始化结果正确；
- 无 TLS 场景同样通过；
- 不引入其它门禁/架构回归。

## 范围约束

- 由 exec-mcc / exec-toolchain 排查修复（mcc x86_64 后端 + mt/ld PC32 reloc），doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
