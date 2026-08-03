---
name: MIR mem2reg 与 LOADFWD 分工 + bridge.c 多 phi 修复
description: mem2reg 与 LOADFWD 互补不冗余（实测边际 5984 行）；bridge.c phi 链式追加是归并必保的真 bug 修复
type: project
---

chloe 的 MIR mem2reg pass 带出两个非显而易见的结论。已合入主线：代码 `b8225ad`，配套文档 `18bb053`（progress.md「MIR alloca 提升 mem2reg」一节，含「bridge.c 预存在 bug 修复（勿 revert）」小节）。

## 1. mem2reg 与 LOADFWD 是互补关系，不可互相替代

管线顺序（`optlevel >= 2`）：`MEM2REG → COPY → LOADFWD → GVN → COPY`。

- **mem2reg**：只提升地址完全不逃逸、标量、访问类型一致、大小匹配的槽。提升后 alloca 本身消失，跨块（循环/分支）全覆盖。
- **LOADFWD**（bella #156，628f17b）：处理 mem2reg **主动拒绝**的槽——地址逃逸（传 call/被取地址）、聚合体、base+off 计算地址、未定值读。这些槽无法整体提升，但块内 store→load 转发仍合法。

**实测**：用编译器自身 90+ 源文件对比 asm 行数，mem2reg+LOADFWD = 755840 行，仅 mem2reg = 761824 行，**LOADFWD 边际净减 5984 行**。

**Why:** 两者看起来都在消除"前端标量经槽位 store/load 往返"，容易被误判为冗余而删掉其一。
**How to apply:** 后续若有人提议移除 LOADFWD 或调换顺序，先要求复现这个 5984 行对比。顺序不能反——LOADFWD 在前会白做一遍马上被 mem2reg 删掉的槽。

## 2. src/lir/bridge.c 多 phi 链式追加（归并必保）

原代码 `qb->phi = phi;` 在循环内每轮覆盖，**一个块有多个 phi 时只留最后一个**，其余 phi 目标值变无定义。改为 `Phi **phitail = &qb->phi; ... *phitail = phi; phitail = &phi->link;`（约 10 行）。

**Why:** 这是预存在的真 bug，此前从未暴露是因为没有任何 pass 会在单块里造多个 phi；mem2reg 是第一个（每槽一个 phi）。
**How to apply:** 归并 bridge.c 时若遇冲突，这段链式追加**必须保留**。后续任何产生多 phi 的 pass 都依赖它。若看到有人 revert 成 `qb->phi = phi;`，那是回归。

## 踩坑规避（MIR pass 通用）
- 移除指令会使 `MVal.def` 失效：chloe 的做法是删 store 时**就地改 MOP_NOP**（不移动数组索引，NOP 留给 DCE 收），压缩块指令数组后统一刷 def 回指；bella 的做法是用前向 `is_alloca` 位图。两者都可行。
- 不要边遍历边删：先标记、跑完再统一压缩。
