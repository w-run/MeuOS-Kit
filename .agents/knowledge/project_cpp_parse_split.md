---
name: cpp_parse.c 大文件拆分方法论
description: 拆分 9088 行 cpp_parse.c 为多小文件的可复用方法 + 自举一致性坑
type: project
---

# cpp_parse.c 大文件拆分方法论（2026-08-05）

## 现状
把 `projects/mcc/src/cpp/parse/cpp_parse.c`（9088 行）拆成 ~600-800 行小文件。已拆出 6 个（均为纯重构、`make check` + verify-all 全 PASS）：
- `cpp_newdel.c`（new/delete/throw）
- `cpp_fold.c`（fold 表达式）
- `cpp_lambda.c`（lambda 表达式）
- `cpp_mangle.c`（名称 mangling）
- `cpp_vtable.c`（虚函数/vtable）
- `cpp_tmpl_member.c`（成员模板）
cpp_parse.c 当前 6489 行。共享符号统一放 `cpp_internal.h`。

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
