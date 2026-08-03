---
name: C++23 四缺口闭环 + token 回放坑
description: 2026-08-03 grace 完成 C++23 P0849/P1774/P1401/P2360 + nodiscard；peek/tokpush 语义坑
type: project
---

2026-08-03 mcc-team-r5，grace 完成 C++23 纯缺口 4 项（worktree-tmp-grace-cpp23，6 commit 已推 origin，基于主线 1ef0a9a），门禁 check-cpp-neg/func/c-mir 全绿。

- **P0849 `auto(x)`**（f7e313a）：expr_primary 增 TAUTO case，识别 `auto(`，decay + 剥顶层 cv 产生 prvalue。注意 `peek()` 命中时会消费该 token（见下）。
- **P1774 `[[assume(expr)]]`**（b54c8b9）：attr.c PREFIXNONE 识别 assume，要求括号参数形式，表达式未求值 no-op。
- **P1401 if constexpr 窄化转 bool**（16c2ca5）：cpp_if_constexpr 条件检查 PROPINT→PROPSCALAR，指针/nullptr 常量接受。
- **P2360 init 语句 alias + C++11 using 别名**（2a4d655）：cpp_using_decl 支持 `using Name=Type;`（改非 static 入 cpp.h）；decl() 顶部路由 CPP_TUSING；if 语句 cpp_if_has_init() 扫描深度 0 分号区分 init 与 ctor-call 条件。
- **属性语义**（da7a107）：struct decl 增 isnodiscard，表达式语句丢弃 nodiscard 返回值发 WARN_RETURN 警告（cc_warn 首个消费者；警告不阻断构建）。alignas 本已生效；deprecated/fallthrough/maybe_unused 使用点警告留待后续。

**坑 1：`peek(kind)` 命中时会消费 token**（src/c/lex/pp.c）：peek 内部 next() 两次、命中后不恢复。初版 auto(x) 在 peek 成功后再次 next() 导致跳过表达式。

**坑 2：`tokpush()` 不回退 scanner**：扫描前瞻后 token 回放必须把「缓冲区域后一个 token」（当前全局 tok）也压栈，否则 ctx 栈耗尽后 scanner 从越过位置继续，token 流错位。range-for 重写（stmt.c）先 push after_body 再 push out.toks 即此模式。

**坑 3：P2360 不能先试 `decl(s,f)` 再判定**——decl 对 ctor-call 表达式（`Vec(3)`）不回溯安全会直接 error；必须先扫描判定有无深度 0 分号再决定解析路径。
