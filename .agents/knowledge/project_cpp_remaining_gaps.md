---
name: C++ 剩余缺口三项闭环（cpprest）
description: 2026-08-03 grace 补依赖类型 NTTP / constexpr 返回类对象 / consteval 模板边界；关键坑
type: project
---

2026-08-03 mcc-team-r5，grace 在 worktree-tmp-grace-cpprest（基 fc1f279）补 C++ 剩余标准缺口，最终 HEAD 3102b7b，verify-all 双模式 19/19。

- **依赖类型 NTTP（ef89d22）**：模板参数表解析期建 ps 作用域，类型参数 T 以 DECLTYPE 入作用域，`template<typename T, T N>` 识别为依赖 NTTP（is_dep_nttp，nttp_type=NULL，实例化绑实参类型）。
- **constexpr 函数返回类对象（3ae5a04）**：解释器 DECL 支持 struct 局部聚合初始化（parseinit + 成员值表）、RETURN 类返回（g_cexpr_class_ret → g_cexp_ret_members 以 call 节点为键）、eval TMUL 折叠支持 &IDENT/&EXPRCALL/EXPRCALL 三种对象形态、defineobj 类 constexpr 变量从调用初始化。
- **consteval 模板边界（d061167）**：实测 alice 已完整实现（consteval 函数模板、constexpr 内常量实参、template<int N>、依赖 NTTP 交叉、非常量实参报错），本 commit 仅补测试闭环。

**关键坑**：
- cpp_tmpl_explicit_parse 原 trial-probe 缓冲：type probe 的 `typename()` 会穿过 TSEMICOLON guard 吞后续 token（混合实参 f<int,5> 失败）。重写为按模板参数类型（is_nttp）直接解析（同类模板路径）。
- cpp_tmpl_const_arg 缓冲循环只在 TGREATER 停止，多值实参 D<2,4> 被整体消费；补 TCOMMA 深度 0 停止。
- 成员访问 AST 形态：左值 `p.a` → `*(&p+off)`（UNARY TBAND of IDENT）；右值调用 `make_p(3).a` → `*(&make_p(3)+off)`（UNARY TBAND of EXPRCALL）——折叠需识别三种形态。
- mcc expr enum：EXPRIDENT=0, EXPRCONST=1, EXPRSTRING=2, EXPRCALL=3, EXPRBITFIELD=4, EXPRINCDEC=5, EXPRCOMPOUND=6, EXPRUNARY=7, EXPRCAST=8, EXPRBINARY=9。
- worktree /tmp/mxx-wt-grace 缺 projects/sysroot，需 ln -s 主 worktree 的 sysroot 才能跑 check-c23。

**未做（既有边界）**：`P{...}` 函数式花括号初始化（直接列表初始化 gap，独立于本任务）；文件作用域 `constexpr P q = make_p(5)` 受 IR 发射器限制（调用初始化器无法发射为静态常量）；类参数传入 constexpr 函数。
