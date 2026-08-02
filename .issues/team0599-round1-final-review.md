# 第一批 mcc-team 收尾（worker-judge 整合报告，2026-08-03）

> 分支：worktree-mxx-work（本地 HEAD=f8f0044，与 origin 同步）
> 角色：reasoning 裁判 worker（整合 / 仲裁 / 审核，不直接写业务代码）
> 注：本应并入 `.issues/0802.md` 的「第一批 mcc-team 收尾」段，但 0802.md 当前正被其他 worker 并行编辑（缺陷 F/U 闭环段），故独立成文，最终由 worker-doc 统一收口。

## 0. 裁判基线（实测）
重建 `m++`（`projects/mcc: make m++`）后，修复前（基线 6142ec5/c38d819）用 `m++ --specs=host` 复现：
- Q `new_delete_nullptr.cc` → 运行 **rc=139**（段错误）
- R `concept_param_rename.cc` → 编译 `undeclared identifier: X`
- S `lambda_capture_class.cc` → 运行 **rc=1**（copies!=1，期望 copies==1/f()==101）
- T `lambda_nested_capture.cc` → 编译 `cannot capture variable 'base'`
- 回归基线：`make check-cpp-func` / `check-cpp-neg` 全绿。

## 1. 审核框架（验收准则 + 根因定位）
| 缺陷 | 验收（修复后必须） | 根因定位（diff 审核重点） |
|------|------------------|--------------------------|
| Q | `new_delete_nullptr.cc` rc=0；delete/delete[] 均 null no-op | `cpp_parse.c:2687 cpp_parse_delete_expr`，dtor+free 前先判空 |
| R | `concept_param_rename.cc` rc=0（X/A 形参名均通过）+ `concepts_combo_boundary.cc` rc=0 | `expand_concept_body`/`expand_constraint_tokens`：实参按逗号切分 + 嵌套 concept use 实参 nargs 少算 2 |
| S | `lambda_capture_class.cc` rc=0（copies==1, f()==101） | `cpp_lambda_expr` 合成 closure ctor 时按成员类型走 ctor（`cpp_lambda_cap_needs_ctor_init`），非位拷贝 |
| T | `lambda_nested_capture.cc` rc=0（内层再捕获外层 base，2~3 层嵌套） | `cpp_lambda_expr` 捕获解析：scopegetdecl 失败时用 `cpp_member_ident` 解析为外层闭包成员 `(*this).name` |

## 2. 缺陷状态表（最终，team-lead 核对版）
> 编号体系（2026-08-03 起）：「组件前缀 + 两位 hex」——cpp-/c-/mir-/x86-；旧字母保留对照。

| 缺陷（新编号） | 状态 | 提交哈希 | 验证 |
|------|------|----------|------|
| cpp-0a（Q） | ✅ closed | f8f0044 | delete/delete[] nullptr 判空 no-op；`new_delete_nullptr.cc` 转正 rc=0 |
| cpp-0b（R） | ✅ closed | 93ab4b4 | concept 实参 span 切分 + 嵌套 use nargs；`concept_param_rename.cc`/`concepts_combo_boundary.cc` rc=0 |
| cpp-0c（S） | ✅ closed | f8f0044（混入） | lambda 按值捕获类对象走 ctor（`cpp_lambda_cap_needs_ctor_init`）；`lambda_capture_class.cc` rc=0 |
| cpp-0d（T） | ✅ closed | f8f0044（混入） | 嵌套 lambda 捕获外层成员（`cpp_member_ident`）；`lambda_nested_capture.cc` rc=0 |
| mir-01（V） | ✅ closed | 93ab4b4（夹带于 R 提交） | MIR msimp 有符号 div/rem 误编译，削减限 UDIV/UREM；`signed_div_pow2.c` 双路径 PASS |
| cpp-0e（U/Z） | 🔄 **open**（worker-lambda 在途，P0） | — | size-0 空类按值传参/return 编译崩溃（cpp 前端 cpp_parse.c 路径）；worker-cpp20 复现 MIR=0/1 双路径均崩；探测 `pending/value_param_member_call.cc` |
| mir-00（F） | ✅ closed | 647a05b（夹带） | MIR fold shl/sar(x,0) 优化缺口，2026-08-03 复核闭环（0802.md 缺陷 F 段） |
| c-00（W） | 🔄 open（worker-mir-tests 登记待排） | — | u8 字面量类型（第二轮） |
| c-01（X） | 🔄 open（worker-mir-tests 登记待排） | — | extern inline（第二轮） |
| cpp-0f（Y） | 🔄 open | — | `delete (T*)expr` 解析失败（low，第二轮） |
| x86-00（va_list 溢出） | ✅ closed | 222a28d | `mabi_vaarg` overflow 推进固定 8 字节槽；`varargs_overflow.c` 双路径 rc=0 |

**回归验证（基于 HEAD=f8f0044）**：`make check-cpp-func` 全绿（含 lambda.cc、new_delete_nullptr、concepts_combo_boundary、struct_multinher 等）；`make check-cpp-neg` 通过（rc=0）；现有 lambda 测试无破坏。

## 3. 并发仲裁结论（关键）
1. **功能全部闭环**：Q/R/S/T 经独立 canary 复验均 rc=0，0 回归。根因均落在正确位置（判空 / 实参切分 / ctor 重载 / 外层成员解析），非症状掩盖。
2. **提交粒度问题（需纠正）**：提交信息与实际缺陷范围严重不符——f8f0044 提交信息仅写「defect Q」，实际 diff 含 **Q+S+T** 三缺陷修复；93ab4b4 提交信息仅写「defect R」，实际也夹带修复了**缺陷 V**（empty-class by-value return 编译崩溃，见 75b0853）。R/Q 实现均正确闭环，但按提交信息无法追溯 S/T/V 的归属哈希。→ 建议在 0802.md 明确记录「S/T 随 f8f0044 闭环、V 随 93ab4b4 夹带闭环」，并要求后续修复按缺陷独立提交，避免贡献者按提交信息误判缺陷状态。
3. **pending canary 转正进行中**：工作树当前有 worker 操作——`pending/lambda_capture_class.cc`、`pending/lambda_nested_capture.cc` 标记删除（staged），`test/cpp/` 下新建同名校验（untracked，待 add）。这是 0802 规划的正确收尾动作，待 worker-test/worker-lambda 提交。注意：`concept_param_rename.cc` 与 `value_param_member_call.cc` 仍留 pending/，前者可随 R 转正、后者（缺陷 U 范畴）随 U 处理。
4. **mxx-c2fix 代码域重叠（高优先级残留）**：`origin/worktree-mxx-c2fix@42b30b1`「C.2 known-limitation fixes (7 items)」改动 `src/cpp/parse/cpp_parse.c` **+345 行**，与 Q/R/S/T 修复域（同文件）完全重叠且独立未并入。当最终合并 mxx-work 时，cpp_parse.c 上将形成三路冲突。→ 建议：先 `merge origin/worktree-mxx-c2fix`（或 cherry-pick 其 cpp_parse.c 部分）到 mxx-work，再统一 rebase/解决 Q/R/S/T；切忌各自 rebase 覆盖。
5. **0802.md 并行编辑**：缺陷 F/U 闭环段与本文档并行撰写中，由 worker-doc 统一收口，避免重复。

## 4. 残留风险
- f8f0044 混入 S/T，使 S/T「提交哈希」追踪不清晰（文档应注明）。
- S 仅验证类拷贝 ctor 调用（lambda_capture_class.cc）；更复杂场景（捕获含 ctor 的类 + operator() 体引用、移动语义交互）待 `lambda_capture_boundary.cc` 转正后回归。
- T 验证 2~3 层同形参名嵌套；混合捕获（`[a]` 外层 + `[b,a]` 内层）、引用捕获交互待补充边界用例。
- 缺陷 mir-01（旧 V，MIR msimp 有符号 div/rem 误编译，负数用例 -7/2 等）：已随 93ab4b4（R 提交）夹带闭环，双路径实测 PASS；回归收口 4c24bfe（signed_div_pow2.c 四实际用例 -7/2/-7%4/-17/8/-17%8 + pass_test Test 3c `test_simpl_sdiv_pow2_exact` 有符号 div/rem 不削减 + Test 3d `test_fold_signed_pow2_values` 常量折叠按值断言），文档补记 7890a35，均合入。缺陷 cpp-0e（旧 U/Z，size-0 类按值传参段错误，探测文件 value_param_member_call.cc）**仍 open**（0802.md 703 行记录纠错，0802 队列表已重命名 U→Z→cpp-0e 并保留 pending/ 待修复）。
- mxx-c2fix 合并冲突（见 §3.4）为最高优先级待处理项。

## 5. 第二批任务建议
1. **收尾**：S/T canary 转正提交（进行中）+ 更新 0802.md 状态表（Q/R/S/T→closed，注明哈希）；concept_param_rename.cc 转正到 concepts 组合回归。
2. **文档校正**：0802.md 注明 S/T 实际随 f8f0044 闭环；缺陷 U 状态与范围再确认。
3. **合并 c2fix**：执行 §3.4 合并方案，解决 cpp_parse.c 三路冲突，回归 `check-cpp-func/neg`。
4. **回归门禁**：每次 push 跑 `make check-cpp-func check-cpp-neg` + C 路径 `make check-c99/c11/c23/mir`（缺陷 U 已验证全绿，但 c2fix 7 项需复验）。
5. **残留缺陷**：缺陷 U（size-0 类值传参段错误，0802 记录仍活跃）待分配修复；C++20/23 缺口调研、va_list 溢出、fold 优化加码（已在队列）。
6. **自举门禁**：worker-selfhost 维持 mcc 自编译 m++ 绿（c2fix 合并后尤需 verify-all 多轮）。

---
（本报告为 worker-judge 终态整合；S/T canary 转正提交落地后如哈希/状态有变，由 worker-doc 同步 0802.md。）

---

## 6. 缺陷 V/U 编号澄清 + 双路径验证（worker-judge 增补，2026-08-03）

### 编号澄清（重要，避免后续混淆；2026-08-03 起迁移为组件前缀+hex）
`0802.md` 队列表（worker-test/worker-doc 维护）的定义，两个缺陷**严格分离**（新编号）：
- **缺陷 cpp-0e（旧 U/Z）= size-0 空类按值传参/return 编译崩溃**（cpp 前端 `cpp_parse.c` 路径；探测 `pending/value_param_member_call.cc`）——🔄 **open**（P0，worker-lambda 在途；worker-cpp20 复现细化：MIR=0/1 双路径均崩，0802.md:281/703）。
- **缺陷 mir-01（旧 V）= MIR msimp 有符号 div/rem 被强度削减误编译**（负数用例 -7/2、-7%4、-17/8、-17%8）——✅ **closed**（93ab4b4 夹带，0802.md:725）。

⚠️ 曾出现编号混淆（team-lead 广播「缺陷 U（MIR div/rem）」、worker-fold 测试注释「defect V」指向同一 div/rem 问题；本文档早期版本也误标）。现统一：**cpp-0e=cpp 前端空类崩溃（open），mir-01=MIR 后端 div/rem（closed）**，与 0802.md 281/725 行一致。

### 缺陷 V 双路径验证（worker-judge 亲自实测，mcc 基于 f8f0044）
| 路径 | 命令 | 结果 |
|------|------|------|
| MIR 默认 | `./mcc --specs=host signed_div_pow2.c` | **PASS**（-7/2=-3 等全部断言通过） |
| legacy | `MCC_USE_MIR=0 ./mcc ...` | **PASS** |

修复有效且不波及 legacy。回归测试 signed_div_pow2.c 已含负数+正数用例（-7/2=-3、-7%4=-3、-17/8=-2、-17%8=-1 等）。

### 缺陷 V 回归收口（worker-fold，已合入）
- **修复代码**：93ab4b4（passes.c msimp_block 削减限 UDIV/UREM，随 defect R 夹带）
- **回归测试**：4c24bfe（signed_div_pow2.c 四实际用例 + pass_test Test 3c `test_simpl_sdiv_pow2_exact` + Test 3d `test_fold_signed_pow2_values` 常量折叠按值断言）
- **文档补记**：7890a35（0802.md V 段回归行补 Test 3d + 测试哈希）
- 验证：check-mir 全绿、verify-all 6/6 PASS（含自举 check-sysroot-static）。worker-fold 工作树已净。

### 门禁缺口（worker-selfhost 确认 + check-c-mir 补跑）
`verify-all.sh` 第 90 行跑 `make check-mir`（MIR 单元测试），**未调用 `check-c-mir`（mir_matrix.sh MIR/LIR 双路径矩阵）**；worker-selfhost 确认 verify-all.sh 全文无 `check-c-mir`/mir_matrix 调用（Makefile:210 目标存在但未纳入门禁）。→ 列「门禁改进项」（第二批）：把 `make check-c-mir` 纳入 verify-all.sh。
**缺口已手工补跑**（worker-selfhost，HEAD a20f08a）：`make check-c-mir` 刚跑过 **70 项 ok、fail=0、exit 0**，明确包含 `ok  c99/signed_div_pow2 (MIR=1 == MIR=0)`——缺陷 U/V 回归用例在 MIR 默认与 legacy 路径结果一致且均 PASS。legacy 路径已实际验证，缺口不影响 V 闭环结论。

---

## 7. 其他审核项（worker-judge 实测，2026-08-03）

### va_list 溢出路径修复（worker-va，222a28d）
- 根因：`mabi_vaarg` 的 overflow 路径用 `oinc`（reg_save_area 槽宽，FP 类 16）推进 `overflow_arg_area`，但 SysV x86_64 栈参数统一 8 字节槽 → 第一个溢出 double 后指针跳槽，后续 FP vararg 读错地址（10 FP varargs 时 sum 45≠55）。修复：overflow 推进固定为 8，reg 路径保留 `oinc`。
- 审核：**根因修复正确**（overflow 是连续 8 字节栈槽，与 reg_save_area 16 字节 FP 槽语义不同；bridge 路径 selvaarg 早已用 8 佐证）。回归测试 `test/c99/varargs_overflow.c` 覆盖 GP/FP/mixed/寄存器耗尽四种溢出形态。
- 实测（mcc 重建后，`-I../sysroot/usr/include`）：`MCC_USE_MIR=1` → **rc=0**；`MCC_USE_MIR=0` → **rc=0**。双路径 PASS。

### 缺陷队列新增（worker-doc 登记，待后续处理）
- **缺陷 W**：u8 字面量类型（open）；**缺陷 X**：extern inline（open）；**缺陷 Y**：`delete (T*)expr` 解析失败（low/open）。均不影响已闭环的 Q/R/S/T/V。

### S/T canary 转正（已随 fe1d55c 合入）
`test/cpp/lambda_capture_class.cc`、`test/cpp/lambda_nested_capture.cc` 已从 pending/ 转入 `test/cpp/`（fe1d55c 夹带，456718f 记录归属），`pending/` 仅剩 `concept_param_rename.cc`、`value_param_member_call.cc`（分别随 R 转正 / 缺陷 U 处理）。check-cpp-func 自动收集含二者。

---

## 8. verify-all 门禁实测（worker-judge 实测 + worker-selfhost 复核，2026-08-03）

worker-judge 实测 `sh test/verify-all.sh --verbose`（mcc/m++ 已重建，HEAD 含全部修复）：
- **PASS=6 FAIL=0 SKIP=0**：`make check` / `check-mir` / `check-cpp` / `check-c99 (MEUOS_SYSROOT)` / `check-c11 (MEUOS_SYSROOT)` / `check-sysroot-static`（自举 mcc 编译 mcc + 运行 hello）
- 六项全 PASS（含自举）——team-lead 核验项 (2) 满足。

worker-selfhost 复核（连续两轮）：
- **R3@222a28d、R4@a20f08a 均 PASS=6 FAIL=0 SKIP=0**，R4 为含 93ab4b4+4c24bfe 修复后的最新状态，无新增 FAIL；R4 本地与 origin 0 divergence。
- check-c99 目标（Makefile:188）用 `for t in test/c99/*.c` 通配循环，必然包含 signed_div_pow2.c 且 PASS；check-sysroot-static 自举输出 "mcc (MeuOS C Compiler) 0.1.0"。
- check-c-mir 补跑 70 项 ok（含 `c99/signed_div_pow2 MIR=1==MIR=0`），legacy 路径已实际验证。

- ⚠️ 门禁缺口（改进项，不影响闭环结论）：verify-all.sh 未调用 `check-c-mir`；已列 §6 门禁段，建议 worker-selfhost/worker-doc 第二轮纳入 verify-all.sh。

## 9. 93ab4b4 / f8f0044 夹带处理（team-lead 裁决）
- **不拆分、不 force-push**：f8f0044 已是多 worker pull 基点，重写祖先链破坏并发。提交归属不纯净的代价低于重写历史代价。
- 在 0802.md 注明实际归属：R 提交实属 cpp_parse.c 修复，V 实属 mir/passes.c 等 4 文件（passes.c/signed_div_pow2.c/pass_test.c/0802.md），归属已混但功能正确。
