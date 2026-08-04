# mcc 常量折叠丢失负零符号（-0.0 折成正零）

> 状态：✅ 已闭环（2026-08-04 根因由 exec-integration-lite 初判、exec-mcc-gp 实测修正并修复）
> 关联 commit：`c004de8`（origin/tmp/exec-mcc/const-fold-negzero）

## 现象

`printf("%f", -0.0)` 且**同 TU 含 +0.0 常量**时，输出 `0.000000`/`0`（应为 `-0.000000`/`-0`）。

## 根因（2026-08-04 exec-integration-lite 初判 → exec-mcc-gp 实测推翻）

- **非 libc**：libc `fp_fmt.c` / `src/stdio/fp_fmt.c` 对真正负零是正确的（用变量传 `-0.0` 时输出正确）——root cause 在 **mcc 编译器**；
- **并非 passes.c 折叠层**：exec-integration-lite 初判 `src/mir/passes.c L525-528/L556-557`（msimp_block NEG 折叠）为漏洞，exec-mcc-gp 实测后**推翻**——passes.c 折叠无泄漏；
- **真正漏洞**：**x86_64 后端逐函数浮点常量池 `fp_label`**（`src/target/x86_64/x86_64_memit.c` L154-169）用**数值相等** `u.d == c->u.d` 去重常量，IEEE 下 `-0.0 == +0.0` 但位模式不同（0 vs 0x8000000000000000），负零被并到正零；
- 同 TU 同时含 +0.0/-0.0 时负零被并成正零；只出现负零（无正零）时符号保留。

## 最小复现

```c
printf("%f\n", 0x0p+0);   /* 正零 */
printf("%f\n", -0x0p+0);  /* 负零 */
```
- **mcc**：输出两行 `0.000000`（第二行错，期望 `-0.000000`）；
- **gcc**：输出 `0.000000 / -0.000000` 正确。

## 范围

- `projects/mcc` 的 **x86_64 后端逐函数浮点常量池 `fp_label`**（`src/target/x86_64/x86_64_memit.c` L154-169）——常量去重用**位模式**而非数值相等，保留 `-0.0` 的 IEEE 符号位；
- libc `src/stdio/fp_fmt.c` **无需改动**。

## 验收

- 复现 case 第二行输出 `-0.000000`；
- `make -C projects/mcc check` 全 PASS；
- 不引入其它架构/门禁回归。

## 范围约束

- 修复 mcc 后端 `fp_label` 去重逻辑；doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。

## 修复记录（2026-08-04 已闭环）

- **commit**：`c004de8`（分支 `origin/tmp/exec-mcc/const-fold-negzero`）
- **改动文件**：`src/target/x86_64/x86_64_memit.c`（`fp_label` 常量去重）
- **方案**：`fp_label` 改用 `memcmp(&x->u, &c->u, sizeof float/double)` **位模式**对比，与 `con_pool_find` 一致，区分 ±0；
- **备注**：exec-integration-lite 原 `passes.c` 定位被 exec-mcc-gp **实测推翻**（passes.c 折叠无泄漏）。
- **验证矩阵**：汇编同时含 `.quad 0`（+0.0）与 `.quad 0x8000000000000000`（-0.0）两条常量表项；端到端输出 `-0.000000`/exit=42 类依据最终门禁确认。
