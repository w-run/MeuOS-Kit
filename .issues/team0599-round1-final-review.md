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

## 2. 缺陷状态表（最终）
| 缺陷 | 状态 | 提交哈希 | 验证 |
|------|------|----------|------|
| Q | ✅ closed | f8f0044 | 修复=delete 判空（p!=0 分支守护 dtor/cookie/free）；canary `test/cpp/new_delete_nullptr.cc` 转正；运行 rc=0 |
| R | ✅ closed | 93ab4b4 | 修复=concept 实参 span 切分 + 修正嵌套 use nargs；`concepts_combo_boundary.cc` 去 `#if 0` 回归 rc=0 |
| S | ✅ closed | f8f0044（混入，见 §3） | 修复=按捕获类型走 ctor 初始化；`lambda_capture_class.cc` rc=0（copies==1, f()==101） |
| T | ✅ closed | f8f0044（混入，见 §3） | 修复=`cpp_member_ident` 解析外层闭包成员；`lambda_nested_capture.cc` 2~3 层嵌套 rc=0 |

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
- 缺陷 V：`empty-class by-value return` 编译崩溃已随 93ab4b4（R 提交）夹带闭环（75b0853 记录）；缺陷 U（size-0 类值传参段错误）同批已 closed。但 m++ 在 x86_64 编译空类边界仍需 worker-selfhost 复验，确保自举不崩。
- mxx-c2fix 合并冲突（见 §3.4）为最高优先级待处理项。

## 5. 第二批任务建议
1. **收尾**：S/T canary 转正提交（进行中）+ 更新 0802.md 状态表（Q/R/S/T→closed，注明哈希）；concept_param_rename.cc 转正到 concepts 组合回归。
2. **文档校正**：0802.md 注明 S/T 实际随 f8f0044 闭环；缺陷 U 状态与范围再确认。
3. **合并 c2fix**：执行 §3.4 合并方案，解决 cpp_parse.c 三路冲突，回归 `check-cpp-func/neg`。
4. **回归门禁**：每次 push 跑 `make check-cpp-func check-cpp-neg` + C 路径 `make check-c99/c11/c23/mir`（缺陷 U 已验证全绿，但 c2fix 7 项需复验）。
5. **残留缺陷**：缺陷 U 的 empty-class by-value return 编译崩溃；C++20/23 缺口调研、va_list 溢出、fold 优化加码（已在队列）。
6. **自举门禁**：worker-selfhost 维持 mcc 自编译 m++ 绿（c2fix 合并后尤需 verify-all 多轮）。

---
（本报告为 worker-judge 终态整合；S/T canary 转正提交落地后如哈希/状态有变，由 worker-doc 同步 0802.md。）
