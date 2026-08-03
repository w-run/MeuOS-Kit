---
name: chibicc B 类修复闭环（narrowing cast + va_end）
description: 2026-08-03 grace 修复窄化 cast 不截断与 va_end 类型检查过严；含 TYPEATOMIC width 未填充的坑
type: project
---

2026-08-03 mcc-team-r5，grace 完成 chibicc B 类两真实 bug 修复（commit fd222ad，已推 origin/worktree-tmp-grace），chibicc 套件 PASS 9→11（cast/decl/varargs 三项），零回归，门禁 check-c99/c11/c23/c-mir 全过。

**Bug 1：convert() 窄化 cast 不截断**（src/c/irgen/convert.c）：原 `dst->u.arith.width <= src->u.arith.width` 一律 return l，int→char/short 等窄化不做掩码。修复：拆为 `<`（真窄化，IAND 按 dst 位宽掩码 + 有符号 ISHL/ISAR 符号扩展，class 按 src->size 取 'l'/'w'，width==32 用 0xFFFFFFFFull 防溢出）与 `==`（保持 return l）。复现：`(_Bool)(char)256` 应 0、`(short)8590066177` 应 513。

**Bug 2：va_end 类型检查过严**（src/c/parse/expr_primary.c）：`va_end(buf)` 传 char* 被 `!typesame(e->type, typeadjvalist)` 拒绝；chibicc 的 va_end 是 no-op 不检查。修复：删除该 error 检查。

**坑：TYPEATOMIC 的 u.arith.width 未填充**：mkatomictype（type.c:144）只设 size/align/base，不设 u.arith.width（u 是 union，读出来≈0）。窄化修复后 `_Atomic(int) y=42` 会被误判成「窄化到 width 0」→ mask=0 → 初始化成 0，导致 atomic_typename RUNFAIL。修复：convert() 顶部对 src/dst 为 TYPEATOMIC 时解包 `src=src->base; dst=dst->base`（原子与 base 表示一致）。**涉及 mcc 算术转换逻辑时，TYPEATOMIC 的 width/size 必须经 base type 读取。**

**Why:** 这两个 bug 是 chibicc 套件登记为 B 类的真实缺陷；TYPEATOMIC 的坑是我自己跑 check-c11 门禁踩出来的二次回归。

**How to apply:** 后续改动 convert()/类型转换相关逻辑时，记得 TYPEATOMIC 需解包 base；chibicc 套件基线 PASS=9/RUNFAIL=6/COMPILEFAIL=26（c622f05），本修复后为 11/5/25。
