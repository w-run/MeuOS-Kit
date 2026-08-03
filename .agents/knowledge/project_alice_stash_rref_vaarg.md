---
name: alice stash@{1} 内容 rvalue reference + va_arg
description: alice 的 mxx-work 在途工作 stash 归属与潜在解锁的意义
type: project
---

2026-08-03 worktree /tmp/mxx-wt-alice 上的 `git stash list` 中：

- `stash@{1}` (mxx-work: 3f0fb1a) **属于 alice**：
  - **C++ rvalue reference (`T&&`) 字段**：mcc.h `struct type` 新增 `bool isrref`；declarator.c `consume(TBAND)` 块在 `t->isref = true` 后追加 `t->isrref = consume(TBAND)` 以区分 `&` 与 `&&`；cpp_parse.c `cpp_mangle_type` 引用 emit `'R'` 改为 `t->isrref ? 'V' : 'R'`，使 `f(Vec)` / `f(Vec &)` / `f(Vec &&)` 三个重载 mangle 名称互不冲突。
  - **x86_64 va_arg ABI 修复**：x86_64_mabi.c 的 `mabi_vaarg` 从单路径近似展开改成完整的 gp/fp offset + overflow_arg_area 双路径；用 limit(48/176) + in_reg mask 做 branchless select；`mabi_selpar` 的 `vafa` 字段额外编码 stack overflow 起始偏移量（`off << 12`）；`mabi_vastart` 改用该 sp 写入 `overflow_arg_area`。
  - `stash@{0}` (worktree-tmp-grace: c622f05) 是 grace 的 WIP，与 alice 无关。eve 此前已合入。

**Why:** alice 在被轮换出 requires 任务前任一阶段（3f0fb1a 后）保存的 WIP；diana 在 2026-08-03 接管 alice #1（requires 表达式四类）任务时撞上该 stash 与 HEAD 的 UU 冲突（declarator.c 已被 eve/hazel 演进过），diana restore HEAD 解锁并保留 stash 完整。

**How to apply:**
- alice 后续若解锁该 stash：建议在干净 worktree 用 `git stash show -p stash@{1} | git apply --3way` 处理潜在合并冲突，先验证 m++ 自举 + check-cpp-* 不退化，再分别提交为 rvalue-ref 与 va_arg 两个独立提交。
- rvalue ref 合入后预期能解锁 m++ move semantics + std::move/std::unique_ptr 等测试场景，是缺陷 K（concept 递归深度）后续概念支持的前置条件。
- va_arg 修复是独立 C ABI 正确性，与 m++ 高级特性解耦，可单独 cherry-pick 到 c2fix 独立修复分支。
- 同事若再次遍历 alice 接管遗留 stash，请先 grep `git stash show -p` 确认内容归属再决定是否清理，不要直接 drop。
