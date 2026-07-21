# P0：基础层与 ar

## 背景

MeuOS Kit 目前依赖宿主 `ar` 归档 `libmcc.a`。需要先建立不依赖宿主 ELF
头文件的内部格式层，并提供可由后续 mcc 自举的 ar 基础实现。

## 本阶段目标

- `libelf` 能安全验证 ELF64 little-endian 文件头和程序/节区表范围；
- `ar rcs/t/p/x` 能处理 15 字符以内成员名；
- 归档输出元数据可复现；
- 构建产物全部进入 `build/`。

## 当前限制

- `r/q` 当前是重写模式，不保留已有成员；
- 尚未实现 GNU `//` long-name table；
- 尚未实现 `/` symbol index；
- 只验证 x86_64 ELF64，不能解析 ELF32 或大端 ELF。

## 完成标准

```sh
make -C projects/meuos-toolchain check
```

通过后才进入 P1 x86_64 汇编器；不允许用宿主 `ar` 代替项目二进制完成 ar 行为测试。
