<!--
priority: P1
status: in_progress
kind: impl
note: 用 meow 原生构建 mcc / meuos-libc / meow；Phase 4 自举链前置
start_ts: 2026-07-24
-->

# 待实现：用 meow 原生构建 MeuOS Kit 自身

## 背景
STATE.md §4 下一步优先级 列出的后续项：用 meow 原生构建 mcc / meuos-libc / meow
自身，Makefile 仅保留 bootstrap/过渡用途。

## 目标
- 为 mcc、meuos-libc、meow 各写一份 `meow.yaml`（替代各自的 Makefile）。
- `meow build mcc` / `meow build meuos-libc` / `meow build meow` 能端到端工作。
- 自举链（bootstrap.sh Phase 1-4）可切换到 meow 驱动。

## 影响范围
- 新增 `pkgs/mcc/meow.yaml`、`pkgs/meuos-libc/meow.yaml`、`pkgs/meow/meow.yaml`。
- 各组件 Makefile 暂时保留，作为 `--bootstrap` 与 sysroot-static 回归的过渡路径。

## 验收
- `meow build meow` 能重建 meow 二进制（等价当前 `make -C meow all`）。
- mcc 与 meuos-libc 的原生构建产物与 Makefile 产物功能等价。

## 验收标准

<!-- TODO(main session): fill in concrete commands. -->

```
make -C projects/meow check
```

