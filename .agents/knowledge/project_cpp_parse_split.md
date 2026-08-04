---
name: cpp_parse.c 大文件拆分方法论
description: 拆分 9088 行 cpp_parse.c 为多小文件的可复用方法 + 自举一致性坑
type: project
---

# cpp_parse.c 大文件拆分方法论（2026-08-05）

## 现状（最终）
`cpp_parse.c`（9088 行）已拆到 **3131 行（-66%）**，拆出 **15 个模块**：
cpp_requires(871)/cpp_newdel(690)/cpp_lambda(656)/cpp_ctor(648)/cpp_method(613)/cpp_vtable(605)/cpp_expr_op(487)/cpp_classtmpl(394)/cpp_tmpl_member(310)/cpp_fold(211)/cpp_freeop(210)/cpp_mangle(185)/cpp_gcctor(132)/cpp_memberlook(105)/cpp_constexpr(1507 先前)
共享符号统一放 `cpp_internal.h`（~240 行）。剩余函数模板核心(~1570行)与 mcc 其它合法文件(cpp_constexpr 1507)量级相当，保持单文件合理。

## 可复用方法（每刀）
1. 选**文件尾部或完全自包含**的段（行号断裂最小、static 依赖最少）。
2. 用 `sed -n 'a,bp'` 机械提取到新文件，避免手抄出错。
3. 跨文件引用的 symbol：**static → 去 static**，声明加进 `cpp_internal.h`（只放确需跨 .c 的）。
4. 类型定义：纯数据 struct（如 cpp_tmpl_param/cpp_tmpl_inst/cpp_template）可移入 cpp_internal.h，用**前向声明 `struct type;`** 即可（全是指针字段），不必 include mcc.h（会与 cpp.h 冲突 redefine tokenkind）。
5. 每拆一个跑 `make clean && make` + `make check`，通过再提交；涉及模板类型时跑 verify-all（含 check-sysroot-static 自举）。
6. git 纪律：文件级 `git commit --only <路径>` + `git add` 新文件，禁 `git add -A`。

## 关键坑：自举一致性（重要）
宿主 gcc 用 `-Wno-all` 编译时**把隐式函数声明当 warning 容忍**，但自举测试 `check-sysroot-static`（用 mcc 编译 mcc 自身源码）**更严格，隐式声明直接报 `E0002 未声明标识符`**。
- 结果：宿主 `make` 通过、`make check` 也过，但 verify-all 的 check-sysroot-static FAIL（Error 127）。
- 教训：拆出 .c 里用到的**任何跨文件函数**（如 cpp_define_method）都必须有原型（cpp_internal.h 或 cpp.h），不能只靠宿主 gcc 宽容。**判断重构是否干净，跑 verify-all 而非仅 make check。**
- 判定已有 vs 新的失败：先 `git fetch` 对齐 HEAD + 复跑确认，勿武断归咎既有（feedback_git §4）。

## How to apply
后续续接拆分时：每拆一刀立即 verify-all（尤其 check-sysroot-static），确保自举一致性；跨文件函数声明务必补进 cpp_internal.h。

## 补充教训（2026-08-05 续，requires/class 簇）
- cpp_parse.c 已从 9088 → 3131 行（-66%），拆出 15 模块。
- **header 原型引用后定义的 struct 必须先加前向声明**：cpp_internal.h 里 `cpp_check_constraint(struct cpp_template *...)` 若写在 `struct cpp_template {}` 定义前，cpp_parse.c 调用点报 `incompatible pointer type`。修：在 header 顶部 forward-declare `struct cpp_template;`。
- **漏 promote 一个 static 会 link 报 undefined reference**：如 `tmpl_param_is_nttp` 被 class 模板簇调用但仍在 cpp_parse.c 是 static。拆簇前先用 grep -oE 列全依赖，逐个核对是 extern 还是 static。
- 拆的簇内部自足函数要给 static 前向声明，否则"static follows non-static"（隐式声明冲突）。
- **提取 .c 时函数返回类型(如 `void`)易丢**：`cpp_record_global_ctor`/`cpp_parse_free_operator` 提取后缺 `void`，宿主 gcc 过但自举 `E0001 期望声明`。提取后务必核对每个函数的返回类型行。

## 使能步骤模式（重要）
拆大量 static 状态的簇前，**先单独做"promote 共享状态到 cpp_internal.h"的独立提交**（不改语义，零行为改变，verify-all 验证通过）：
- g_cpp_method/struct cpp_method_ctx → 解锁 ctor/method 簇
- g_cpp_tmpl_instantiating/binds/nbinds → 解锁 member 缓冲簇
- g_cpp_tmpl_stack/depth/expl_*/pack_* → 解锁函数模板子簇
这类"先开小路再拆"使能提交风险低、可独立验证，是拆深耦合状态机的标准手法。

## 内聚状态机文件拆分（2026-08-05，link.c 成功示范）
cpp_parse.c 拆完后，把方法推广到**单一 `struct ld_context ctx` 状态机的 mt/ld link.c**（4780 行）——之前误判为"内聚单体不可拆"是错的：
- **关键判断**：即使逻辑内聚，只要**状态全装在单个 context 结构 + 指针传递**（无文件级 static 全局），就能按阶段函数域拆（layout/reloc/dynamic/elfout），共享 context struct 放 ld_internal.h 即可。
- **可靠函数边界脚本**（brace-matching）：`^name(` 行后找 `{`，逐行计数 `{`/`}` 到匹配 `}`；向上回溯返回类型行作起点。跨多行注释会夹在函数边界断裂，提取后需清理**孤儿注释 + 悬挂 `};`**（`-Werror` 会立即报错，配合 grep 检查死代码）。
- 跨域函数去 static + header 原型；`-Werror` 下隐式声明即报错，快速暴露漏 decl。函数返回类型必须与 header 一致。
- 每拆一域跑 `make -C projects/meuos-toolchain check`（含 rtld e2e）验证零回归。

## 内聚状态机切分法（link.c/assemble.c 通用，2026-08-05）
- 此法已成功用在 mt/ld link.c（4780→2055，dynamic/reloc/elfout/layout）与 mt/as assemble.c（2406→1464，as_dwarf/as_elfout）。
- 判断标准：**单一 context struct（ld_context/as_file）指针传递、无文件级 static 全局** → 可按阶段/功能连续域切分。
- 若子目标文件**已有 internal header**（如 assemble 的 mt/as_int.h），类型大多已在，只需补声明。
- **#define 宏不随 include 传播**：跨文件用（如 MT_ST_INFO）要放进 internal header。
- **struct 定义不共享 → 'struct X declared inside parameter list' / incomplete type**：被多文件用的 struct 定义必须移入 internal header。
- 提取后清理孤儿注释 + 悬挂 `};`（函数尾的孤立右括号）+ 多余空行；`-Werror` 立即报 expected identifier。



