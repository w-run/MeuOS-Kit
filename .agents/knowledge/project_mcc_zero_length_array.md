---
name: mcc GNU 零长数组与 check-dwarf personality 闭环
description: mcc 支持 GNU 零长数组 (int e[0]) 的 4 处根因修复；check-dwarf __gxx_personality_v0 链接错误根因
type: project
---

# mcc GNU 零长数组 + check-dwarf personality 闭环

**状态**: ✅ 已闭环 (2026-08-08)

## 缺陷 1：GNU 零长数组 `int e[0]`（commit 1c598cdf）

局部 `int e[0];` 触发 `funcmem.c:49 funcalloc: Assertion 'd->type->u.array.size' failed`。根因不是一处，共 4 处：

1. **funcmem.c funcalloc**：else 分支假设「size==0 且 TYPEARRAY 必是 VLA（u.array.size 非空）」。零长数组 `u.array.size==NULL`。修法：零长数组分配 1 字节栈槽（GCC 语义：sizeof=0 但对象有独立地址）。
2. **expr_unary.c sizeof**：`t->kind==TYPEARRAY && t->size==0` 一律走 VLA 运行时路径 EXPRSIZEOF。零长数组也被误判 → `sizeof(e)` 编译成读栈垃圾值（实测返回 1 而非 0）。修法：改为 `t->prop & PROPVM` 才走运行时路径。
3. **expr.c mksizeofexpr**（指针算术 `p+n` 的 sizeof 基）：同样加 PROPVM 守卫。
4. **declarator.c VLA 判断**：判断数组长度是否编译期常量时条件为 `EXPRCONST && base.type->size`。元素类型是零长数组（size=0）时，外层 `int g[2][0]` 被误判为 VLA（报 "static storage duration cannot have variably modified type"）。修法：只看 EXPRCONST；`ULLONG_MAX / base.type->size` 溢出检查对 base.size==0 加保护。

**验收**：`int e[0]` 全局/局部编译通过；`sizeof(e)==0`；typedef/多维/static/函数参数边界全过；struct 尾部零长数组 offset 与 gcc 一致；VLA 功能无回归；`make check` exit=0。

## 缺陷 2：check-dwarf `__gxx_personality_v0` undefined reference（commit 1e7c8965）

**现象**：verify-all 的 check-dwarf（test/dwarf/line.sh，系统 as/ld 裸链接）报 `.eh_frame+0x13: undefined reference to __gxx_personality_v0`。

**根因**：`src/target/x86_64/x86_64_memit.c:1337` `bool has_eh = g_meuos_specs != 0;` —— has_eh 初始化为「--specs=meuos 下所有函数都有异常处理」，导致每个 C 函数都发 `.cfi_personality __gxx_personality_v0` + `.cfi_lsda`，而裸链接无 libgcc → undefined reference。注释本意就是「仅 C++ try/catch 函数（调用 `_meuos_exc_*` helper）才发」。

**修法**：`has_eh` 初始值改为 `false`，仅在扫描到函数调用 `_meuos_exc_*` 时置 true。

**验收**：`make check-dwarf` exit=0；C++ try_catch.cc 在 `--specs=meuos` 下仍生成 7 处 cfi_personality（异常路径无回归）。

## 教训

- 「size==0 的数组」≠ VLA。mcc 中 VLA 的唯一判据是 `t->prop & PROPVM`，零长数组无 PROPVM 但 size 也为 0，凡是以 `size==0` 判 VLA 的路径（sizeof、funcalloc、declarator 常量判断、mksizeofexpr）都要加 PROPVM 守卫。
- 预存失败用基线验证法确认（worktree checkout HEAD 对比），三个 FAIL（loongarch64 gate、i386-runtime float-arith、loongarch64-runtime 宿主 as BFD bug）均为预存/环境问题，与本修复零回归。
