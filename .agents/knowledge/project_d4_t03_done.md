---
name: m++ 缺陷 D4（T03）已闭环
description: 非空类按值返回/构造器标量 init-list 缺陷 D4，由 alice 在 608f31c 修复并由 5db6f26 合入主线；任务描述的"irgen 首成员隐藏指针/sret"路径与此无关
type: project
---

mxx 缺陷 D4（非空类按值返回结果错乱，2026-08-03 文档登记为"待修"，阻塞 P2266 简化隐式移动）已由 alice 在 608f31c（"修复 D1 const T& operator 级联查找 + D4 ctor 标量 init-list 落地"）修复，合入 5db6f26（"Merge worktree-tmp-alice-cpp: cpp_parse D1/D4/D2/E2/E3"）。

**根因（与任务描述不同）：**`src/cpp/parse/cpp_parse.c:emit_base_ctors_for` 对 struct/union 成员发 ctor 调用，但标量成员的 init-list 项 `: a(7)` 被 `continue` 丢弃，类按值返回的成员字段为垃圾。修复：对命中初始化项的非 struct/union 成员发射 `*(this+offset) = v`。

**回归测试：**`test/cpp/ctor_scalar_initlist.cc`（自动接入 check-cpp-func）。当前 HEAD 9aa6d6b 上 verify-all 19/19 PASS。

**Why:** 2026-08-03 用户派 T03 任务时，任务描述沿用了 e06a2b0 旧快照的判断（"irgen/func.c + value.c + expr.c 首成员隐藏指针 / sret 选择"），但实际 D4 已闭环，根因不在 irgen 层 ABI 路径，而在 cpp_parse 层 ctor 发射。

**How to apply:** 派 D4/T03 类任务前先 `git log --all --oneline --grep="D4"` 与 `git branch --contains 608f31c` 确认是否已在主线；若已合入，不要重复实现，改为补回归测试或扩边界场景。若任务描述的修复路径与 commit 信息不符，先 git show <hash> 看实际改动再决定。已建立 wip worktree `/tmp/mxx-wip-d4` 与分支 `worktree-wip-d4-aggret` 后由用户选"取消任务"已删除。

**2026-08-03 r7 复查：** 主线 9aa6d6b 再次派 T03 时，独立 worktree（`/tmp/mxx-pt-d4`，分支 `worktree-wip-d4`，commit `c3fc1e0`）大范围探测仍不重现——x86_64 双后端对 8/12/16/24/40/64/200B、嵌套、混合浮点、非平凡拷贝、虚函数、multi-return 全通过；m++↔gcc 交叉 ABI 双向一致；`ctor_scalar_initlist.cc` 全绿。仅新增扩展回归门禁 `aggregate_return_nonempty.cc` + `return_pair_value.cc`（自动 glob 接入 check-cpp-func）。注意：`projects/mcc/-` 是误提交的 .s 汇编垃圾文件（-MP 依赖副作用），`make -S`/`-o -` 会覆写它，勿 `git add -A` 带进去。