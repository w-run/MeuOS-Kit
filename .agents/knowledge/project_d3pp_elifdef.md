---
name: D3-PP #elifdef 缺陷已由夹带提交预先修复
description: T08 复查结论——#elifdef 跳过组误报在 b8225ad 已修，修复由 6ca4ba1 夹带引入；回归测试补于 5bfd08a
type: project
---

T08（D3-PP `#elifdef` 前组求值误报）在基线 b8225ad 上**已不复现**：代码修复由
commit `6ca4ba1`（主题为 "chibicc run.sh 修复 sysroot 路径推导"）**夹带**引入。
测试缺口由 `5bfd08a` 补齐（分支 `worktree-wip-d3-elifdef-skip`），未改编译器。

**Why:** 缺陷台账把它列为 open，是因为修复藏在一个主题完全无关的提交里，
从 pp.c 的 git log 主题行看不出来（需 `git log -S` 才能定位）。这直接消耗了
一轮 worker 排查成本。

**How to apply:** 开 T 任务派给 worker 前，先花一分钟实际复现确认缺陷仍存在；
台账里可能还有别的条目已被其他提交顺手修掉。定位"某处改动何时引入"用
`git log -S "<代码片段>"` 而非看提交主题——本仓库存在多起夹带，主题行不可信。

**缺陷机理（如日后回归）：** pp.c `skipbody()` 的 TELIFDEF/TELIFNDEF 分支，
条件成立时若在 `expectnewline()` 之前 `return`，`tok` 会停在宏名标识符上；
调用方 `directive()` 要求进入时 `tok == TNEWLINE`，残留标识符即被误判为
指令行多余 token，报 "expected newline after preprocessing directive"。
`#elif` 无此问题（`evalexpr()` 自身停在 TNEWLINE）。

**已知未修的相邻差异：** `#elifdef` 出现在 `#else` 之后时 GCC 报错
"#elifdef after #else"，mcc 静默接受（诊断严格性缺口，非误报，未开单）。
