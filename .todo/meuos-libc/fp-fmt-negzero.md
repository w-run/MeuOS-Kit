# libc fp_fmt 浮点负零格式化缺陷

> 状态：🔄 开放（2026-08-04 由 exec-integration-lite 在聚合全量回归 2d4b65a 中发现）
> 关联 commit：无（非聚合引入，libc 既有缺陷）

## 现象

`make -C projects/meuos-libc check` 的 `fp_fmt` **FAIL 2 项**：
```
%f => [0.000000], want [-0.000000]
%g => [0], want [-0]
```
即 printf 浮点格式化负零（`-0.0`）符号位丢失，输出为正 `0`。

## 非聚合引入判定

- 聚合期间 libc **零改动**：`git diff df962a0 2d4b65a -- projects/meuos-libc/` 为空；
- 故判定为既有缺陷，非聚合回归引入。

## 根因定位（待 exec-libc 域实施时核实）

- libc 的 printf（`dto_digits` 等浮点格式化路径）内部状态 bug：
  - **负零符号位在完整 fp_fmt 多值前置后丢失**（多次格式化复用缓冲/状态时符号位被覆盖）；
  - 独立最小复刻（单值格式化）PASS，完整 fp_fmt 文件（多值含负零）FAIL——指向跨调用状态污染。

## 范围

- `projects/meuos-libc` 的浮点格式化实现（printf / dto_digits 相关源）。

## 验收

- 修复后 `fp_fmt` 全 PASS（`%f`→`-0.000000`、`%g`→`-0`）；
- `make -C projects/meuos-libc check` exit 0；
- 不引入回归（compare/其余 libc 门禁全 PASS）。

## 范围约束

- 由 exec-libc 域实施；doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
