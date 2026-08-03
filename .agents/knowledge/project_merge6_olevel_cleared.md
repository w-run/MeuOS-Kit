---
name: 6 分支归并 + check-olevel 三差距清零
description: 2026-08-03 grace 归并 eve-i18n/alice-cli/hazel-fp/diana-dwarf/chloe-memconst/bella-cmov；-f/-fno-omit-frame-pointer 赋值反 bug
type: project
---

2026-08-03 mcc-team-r5，grace 归并 6 worker 分支到 worktree-mxx-work（基 9742e2f，最终 HEAD 5aa1154，双模式 verify-all 19/19）。

- **归并**：eve-i18n（1ce1b33）/ alice-cli（ac57402，usage.c 冲突）/ hazel-fp（14b00a9）/ diana-dwarf（f68a270，usage.c + worker-deployment.md 冲突）/ chloe-memconst（8d0aace）/ bella-cmov（49dbab6，worker-deployment.md 冲突）。
- **check-olevel 三项差距全部清零**（上一批归并记录的 MIR-native 缺口）：① cmov（bella 机器层 ifconv 通道 + MMOP_CMOV）；② -O2 叶函数帧指针省略（hazel，memit.c g_omit_fp）；③ -O1 内存常量传播（chloe memconst）。实测 check-olevel RC=0。

**集成 bug（47f2cc0）**：main.c 的 `-f/-fno-omit-frame-pointer` 赋值写反：
- `-fomit-frame-pointer` 误设 g_force_fp=1 → 应 0（允许省略）
- `-fno-omit-frame-pointer` 误设 g_force_fp=0 → 应 1（强制保留）
alice CLI 引入，hazel 的 -O2 叶函数省略落地前无影响；修正后 cli-args.sh 断言转绿。

**memit.c 帧指针机制**：g_omit_fp = optlevel>=2 && !g_force_fp && !hascall && !hasrbp && !dynalloc && !vararg；g_force_fp 定义在 x86_64_emit.c（-Og 与 -fno-omit-frame-pointer 共用）。memit 输出用 TAB 分隔（grep 需 \s* 容错）。
