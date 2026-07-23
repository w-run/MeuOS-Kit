<!--
priority: P3
status: done
done_ts: 2026-07-22
note: libelf / ar / ranlib / 宿主 ld 链接 / r-q-t-p-x 全部完成，进入 P1
-->

# P0：基础层与 ar（P0a/P0b 已完成）

## 背景

MeuOS Kit 目前依赖宿主 `ar` 归档 `libmcc.a`。需要先建立不依赖宿主 ELF
头文件的内部格式层，并提供可由后续 mcc 自举的 ar 基础实现。

## 本阶段目标

- `libelf` 能安全验证 ELF64 little-endian 文件头和程序/节区表范围；
- `ar rcs/t/p/x` 能处理 15 字符以内成员名；
- 归档输出元数据可复现；
- 构建产物全部进入 `build/`。

## 当前限制

- `r`/`q` 已支持基本替换和追加，但尚未覆盖所有 GNU/BSD 兼容选项；
- 尚未实现独立 `ranlib` 命令和 BSD `#1/` extended-name member；
- archive 目前整体读入内存，超大归档的流式优化留给后续；
- 只解析 x86_64 ELF64 little-endian symbol index，不能解析 ELF32 或大端 ELF。

## 完成标准

```sh
make -C projects/meuos-toolchain check
```

已通过：symbol index、GNU long-name、宿主 ld 链接、r/q、t/p/x 和 x86_64
ELF fixture。后续进入 P1 x86_64 汇编器；不允许用宿主 `ar` 代替项目二进制完成
ar 行为测试。
