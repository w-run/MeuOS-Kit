# mcc/m++ 重构审计报告

- 审计时间：2026-08-02 07:50 (+0800)
- 审计分支：`worktree-mxx-work`
- 审计基线提交：`fe69a46`（HEAD，同步 origin）
- 审计人：auditor（验收检查型 worker）
- 范围：最近 10 个提交代码审查 + verify-all.sh 门禁基线 + 工作区在途改动编译验证
- 后续更新：问题 1（c93d5f7 门禁回归）已由 **6003f47** 修复（补 `extern cpp_pending_record_depth` 声明），verify-all 自举门禁恢复 6/6 PASS（2026-08-02）。

---

## 1. 门禁基线结果

运行：`sh projects/mcc/test/verify-all.sh`（sysroot 模式，`MEUOS_SYSROOT`）

### 1.1 干净 HEAD 基线（临时 stash 在途改动后，HEAD=`6467d6f` 时）

| 检查项 | 结果 |
|---|---|
| make check | PASS |
| make check-mir | PASS |
| make check-cpp | PASS |
| make check-c99 (MEUOS_SYSROOT) | PASS |
| make check-c11 (MEUOS_SYSROOT) | PASS |
| make check-sysroot-static | PASS |

**基线结论：已提交代码（含最近 10 个提交）在门禁上全绿（6/6 PASS）。**

> 注：审计过程中 HEAD 被团队其他成员推进至 `fe69a46`（成员模板 c93d5f7 已提交 + docs 同步），与 origin 一致。

### 1.2 当前 HEAD + 工作区在途改动基线

| 检查项 | 结果 |
|---|---|
| make check | PASS |
| make check-mir | PASS |
| make check-cpp | PASS |
| make check-c99 (MEUOS_SYSROOT) | PASS |
| make check-c11 (MEUOS_SYSROOT) | PASS |
| make check-sysroot-static | **FAIL**（问题 1，c93d5f7 引入；已由 6003f47 修复，修复后 6/6） |

**1 FAIL：check-sysroot-static，错误为 `expr_postfix.c:288: error: undeclared identifier: cpp_pending_record_depth`。**

该失败**不属于工作区在途改动**——工作区在途改动中不含 `expr_postfix.c`，该文件与 HEAD 一致（`git status` 为空）。问题由**已提交的成员模板提交 `c93d5f7` 引入**（见 §3 问题 1）。

---

## 2. 最近 10 个提交逐项审查

审查范围（`git log -10`，旧→新）：

| # | 提交 | 组件 | 描述 | 审查结论 |
|---|---|---|---|---|
| 1 | 642574b | m++ | C.2.8 class templates | PASS |
| 2 | 2edfb82 | docs | 0802 - class templates done | PASS（纯文档） |
| 3 | c940c34 | mcc | verify-all 脚本 + make check-all 目标 | PASS |
| 4 | 3e1ab86 | as/aarch64 | fix cset/csinc encoding | PASS |
| 5 | fdb95b7 | docs | 0802 - verify-all gate 等 | PASS（纯文档） |
| 6 | 7dbf93d | docs | ARCHITECTURE.md 更新为 MIR 时代 | PASS（纯文档） |
| 7 | 6467d6f | docs | progress.md 修正 P4-P6 状态表 | PASS（纯文档） |
| 8 | e745f17 | docs | cpp-roadmap.md 未实现特性规划 | PASS（纯文档，planner） |
| 9 | c93d5f7 | m++ | C.2.8 member templates | **问题 1（见下）** |
| 10 | fe69a46 | docs | 同步 C.2.8 成员模板完成 | PASS（纯文档） |

### 提交格式检查

全部 10 个提交均符合 `<组件>: <描述>` 格式（`m++:`/`as/aarch64:`/`mcc:`/`docs:`）。PASS。

### 调试残留检查

- 各提交内未发现 `fprintf(stderr)` / `printf` 调试残留（`grep` 复核：`3e1ab86`、`109a3ff`、`84727a6`、`642574b`、`c93d5f7` 均无）。
- `cpp_parse.c` 中若干 `printf(".section ...")` 属于汇编代码生成（`.init_array`/`.fini_array` 段输出），非调试残留，判定合格。

### 未实现声明检查（cpp.h）

核对 cpp.h 中全部 `cpp_tmpl_*` 声明与 `cpp_parse.c` 实现体一一对应，均存在实现，无"声明但未实现"。

### 重点用例回归抽查

- c99/c11 回归：基线全 PASS（§1.1）。
- cpp 重点用例：check-cpp-lex/virtual/func/neg 全部 PASS，`template.cc` 13 个类模板断言 + 成员模板断言在干净基线 PASS。

---

## 3. 发现的问题

### 问题 1（严重，门禁回归；**已修复 6003f47**）— c93d5f7 成员模板提交在 `expr_postfix.c:288` 调用未声明函数

- **位置**：`projects/mcc/src/parse/expr_postfix.c:288`
  ```c
  cpp_pending_record_depth();
  ```
- **现象**：`make check-sysroot-static` 失败（mcc 自举编译 mcc 时）：
  ```
  src/parse/expr_postfix.c:288:7: error: undeclared identifier: cpp_pending_record_depth
  ```
- **根因**：`expr_postfix.c` 不 `#include "cpp.h"`（仅 include util.h/mcc.h/expr_internal.h）。该文件此前对 cpp 前端函数均用局部 `extern` 声明（见 339 行 `extern void cpp_pending_record_depth(void);`）。c93d5f7 在成员模板分支（约 262-300 行）调用了 `cpp_pending_record_depth()`，但**该调用点之前没有前置 extern 声明**——第一个可见声明在 339 行（更靠后）。宿主 gcc 因 `-Wno-all` 将隐式声明降级为 warning 侥幸通过（实测 `cc -Wimplicit-function-declaration` 下即报 error），而 mcc 自举编译器严格要求，直接报错。
- **影响**：`make check-all` 门禁在成员模板提交后不可全绿；自举链路（mcc 编译 mcc）被破坏。
- **修复（6003f47）**：在 `expr_postfix.c` 顶部 extern 声明区补 `extern void cpp_pending_record_depth(void);`（与既有 cpp_pending_* 声明并列）。修复后 `verify-all.sh` 自举门禁恢复 **6/6 PASS**。
- **原建议**：在 `expr_postfix.c` 顶部函数作用域（如 61-67 行 extern 声明区）补一行 `extern void cpp_pending_record_depth(void);`，或在 262 行成员模板块内声明。最小改动即可修复，且与既有成员模板 extern 声明风格一致。✅ 已按此修复。

### 问题 2（低，观察项）— 在途调试输出均为 getenv 门控，但需确认去留

- **位置**（工作区未提交改动，非本次审计提交范围）：
  - `src/emit/emit.c:150,177` — `MCC_DBG_STASH`
  - `src/ir/passes.c:58,61` — `MCC_DUMP_BEFORE_ISEL` / `MCC_DUMP_AFTER_ISEL`
  - `src/target/arm/arm_abi.c:282` — `MCC_DBG_ARM_CALL`
  - `src/target/arm/arm_isel.c:105` — `MCC_DBG_ARM_ISEL`
- **结论**：全部 `getenv()` 门控，默认不输出，符合规范；编译验证通过（toolchain 全 target 构建 + mcc 构建）。但请 owner 在合并前确认这些门控是保留（排障用途）还是移除，避免随功能提交残留。

---

## 4. 工作区在途改动验证（不修改，只报告）

当前未提交改动（相对 HEAD `fe69a46`）：

| 文件 | 内容 | 编译验证 |
|---|---|---|
| `projects/meuos-toolchain/src/target/aarch64/encode.c` | cset 64 位编码（`is64 ? 0x9A9F07E0 : 0x1A9F07E0`） | PASS（toolchain `make` 全量构建，`-Werror -Wall -Wextra -Wpedantic`） |
| `src/emit/emit.c` | MCC_DBG_STASH 调试（getenv 门控） | PASS |
| `src/ir/passes.c` | isel 前后 dump（getenv 门控） | PASS |
| `src/target/arm/arm_abi.c` | MCC_DBG_ARM_CALL（getenv 门控） | PASS |
| `src/target/arm/arm_isel.c` | MCC_DBG_ARM_ISEL（getenv 门控） | PASS |
| `test/cpp/pending/`（untracked） | 9 个 pending 用例 | 未编译（待实现） |

- 在途改动的 aarch64 cset 修复与已提交的 `3e1ab86`（cset/csinc）逻辑一致，是对 64 位寄存器 cset 的补充，构建通过。
- toolchain 测试：`make check`（as/ld/nm/objdump/readelf/strip/objcopy/ar/ranlib）全 PASS；`aarch64_e2e.sh` 中 as+ld 链正常（生成 aarch64 ELF），qemu 环节因本机无 `env/qemu/qemu-aarch64` 二进制而 FAIL（**环境限制，非编码问题**）。
- 在途改动不包含 `expr_postfix.c`，与问题 1 无关。

---

## 5. 审计结论

1. 已提交的最近 10 个提交中，9 个 PASS、格式与文档质量合格。
2. **1 个提交（c93d5f7 成员模板）引入门禁回归**：`expr_postfix.c:288` 调用 `cpp_pending_record_depth()` 无前置声明，导致 `make check-sysroot-static` 失败，`make check-all` 无法全绿。**已由 6003f47 修复**（补 extern 声明，1 行级改动），修复后 verify-all 自举门禁恢复 6/6 PASS。
3. 工作区在途改动（调试输出 + aarch64 cset 64 位）构建验证全部通过，未破坏构建；调试输出均为 getenv 门控，符合规范。
4. aarch64 e2e 的 qemu 环节为环境缺失（`env/qemu/` 不存在），不属代码回归。

**处置**：问题 1 已修复（6003f47）并确认自举门禁 6/6 全绿，审计闭环完成。问题 2（getenv 调试门控去留）待各 owner 合并前确认。
