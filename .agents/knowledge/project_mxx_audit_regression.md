---
name: mxx-work 审计回归记录
description: c93d5f7 门禁回归已由 6003f47 修复闭环；audit-report.md 位置
type: project
---

mxx-work 分支曾存在门禁回归：提交 `c93d5f7`（m++ 成员模板）在 `projects/mcc/src/parse/expr_postfix.c:288` 调用 `cpp_pending_record_depth()` 时无前置 extern 声明，导致 `make check-sysroot-static`（mcc 自举编译 mcc）失败。**该问题已由 `6003f47` 修复**（expr_postfix.c 顶部 extern 区补一行声明），verify-all 自举门禁恢复 6/6 PASS，审计闭环完成（文档记录：`.issues/0802.md`「审计闭环」段 + `projects/mcc/docs/audit-report.md`）。

**Why:** 问题由 auditor（2026-08-02）在验收审查中发现，记录于 `projects/mcc/docs/audit-report.md`（提交 25254fe）；指挥官随后实施修复（6003f47）。根因是 expr_postfix.c 不 include cpp.h、局部 extern 声明位置晚于调用点，宿主 gcc 靠 `-Wno-all` 容忍隐式声明，mcc 自举编译器严格报错。

**How to apply:** 此回归已闭环，无需再按已知问题处理。mxx-work 是共享 worktree，多 worker 并发改动同一文件（在途常见：MCC_DBG_* 调试输出、aarch64/arm c99-float 修复等），审计/操作前先 `git status` 确认；提交文档时只 `git add` 自己的文件，勿 `git add -A`。
