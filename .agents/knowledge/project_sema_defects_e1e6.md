---
name: mcc struct array.len 字节长度坑 + E1-E6 修复
description: 2026-08-03 grace 修复 sema/decl E1/E4/E5/E6；struct array.len 是字节数不是元素数
type: project
---

2026-08-03 mcc-team-r5，grace 完成 sema/decl 组四个 C++ 语义漏检缺陷修复（commit 0123c96 + 83b3feb，已推 origin/worktree-tmp-grace-sema），门禁 check-cpp-neg/func/c-mir 全过。

- **E1** 命名空间/文件作用域对象重定义：decl.c DECLOBJECT 路径，C++ 模式 prior 已定义/暂定定义且非 extern → `redefinition of 'a'`。`extern int a; int a;` 仍合法。
- **E4** 非 void 缺 return：新增 irgen/func.c `func_falls_off_end()`——块 CFG 可达性分析，decl.c 函数定义处 C++ 模式报 `control reaches end`。放过 while(1)/死代码/常量条件分支，拒绝 `if(x) return` 缺 else。
- **E5** 引用未初始化：decl.c `d->type->isref && !hasinit` → `reference must be initialized`。
- **E6** C++ 禁用 VLA：decl.c `t->prop & PROPVM` → `variable-length array not allowed`。constexpr 常量长度不误报；plain `const int n=5; int arr[n]` 在 mcc 前端本当作 VLA（C 模式亦然）——这是前端「plain const 不作常量表达式」的既有限制，非 E6 引入。
- **E4 后续（db13604）** GNU `__attribute__((noreturn))` 误报修复：GNU attr 的 kind 在 declspecs/declaratortypes 被丢弃（declspecs 用 `gnuattr(NULL,...)`，declaratortypes 局部 `struct attr` 只传播 align），导致 isnoreturn 不置位。修复：`struct qualtype` 增 `kind` 字段 + declarator/declaratortypes 增 `struct attr*` 出参，decl.c 合并 `a.kind |= base.kind` 并传 `&a`。`[[noreturn]]`/`_Noreturn` 本就正常（decl.c:178 attr / funcspec），只有 GNU 形式漏。
- **E4 行号定位（15b6b08）** `control reaches end` 报错原用 `&tok.loc`（函数体后的下一 token）落到下一声明行。修复：`struct func` 增 `bodyend` 字段，stmt.c 复合语句在消耗 `}` 前记录 tok.loc（最外层函数体的 } 最后被记录），decl.c 经 funcget_bodyend 取位置报错。注意 stmt.c/decl.c 无 struct func 全定义，需经 setter/getter（声明在 mcc.h、实现在 func.c）传递。

**坑：mcc `struct array.len` 是字节数而非元素数**（util.h `struct array`，arrayaddptr 每次 +sizeof(void*)=8；arrayforeach 宏按字节推进）。用 `w[--work.len]` 按元素下标会段错误。正确取栈顶：`w[work.len / sizeof b - 1]` 再 `work.len -= sizeof b`。

**Why:** 修 E4 时初版 func_falls_off_end 按元素下标 pop 导致 SIGSEGV，gdb 定位为 len 语义误解；该坑在遍历 struct array 时容易复发。

**How to apply:** 在 mcc 代码中遍历 `struct array` 时一律用 arrayforeach 或按字节数换算；手写下标务必 `len/sizeof(T)`。
