# mt/as 局部数字标签 forward/backward 解析缺陷

> 状态：✅ 已闭环（2026-08-07 toolchain-pie-worker）
> 修复 commit：`43c72b5d`（mcc-dev，resolve_numeric_references in assemble.c）
> 回归门：`test/as_locallabel.sh`（check-as-locallabel）

## 现象

- exec-toolchain-gp 修 mt/ld PIE GOT/PLT（`25bbee1`）时发现：mt/as 对**局部数字标签** `1f`/`2f`（forward/backward reference）的分支目标**解析错**；
- 如 `jne 1f` 跳到**错误位置**而非预期失败路径。

## 判定

- mt/as 既有缺陷，与 mt/ld PIE 修复无关（修复过程中顺带暴露）；
- 影响**手写汇编**（asm）中数字局部标签的跳转目标在 forward/backward 引用时可能选错；
- **mcc 生成的汇编用命名标签，不受影响**（mcc 产物安全）。

## 范围

- mt/as 的**局部标签解析**：`src/target/x86_64/encode.c` 的 `normalize_numeric_reference`（L117）及 label 解析路径——当前把纯数字引用（以数字开头且无 `@`)括处理）判为数值并清零 symbol，未正确区分数字局部标签的 `f`/`b`（forward/backward）语义；
- 需补全前向引用（`1f` 指后续第一个标号）与后向引用（`1b` 指前面最近的标号）的解析绑定。

## 验收

- `jmp 1f` / `jne 1f` / `jmp 2b` 等 forward/backward 数字局部标签在 mt/as 正确解析、跳到正确目标；
- 不影响 mcc 产物（命名标签路径）；
- `make -C projects/meuos-toolchain check` 不引入回归。

## 范围约束

- 由 exec-toolchain-gp 修复 mt/as 局部标签解析；doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
