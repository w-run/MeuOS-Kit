---
name: chibicc 套件 + sema/PP 修复
description: chibicc 套件根因与 B 类修复、sema E1-E6、D3-PP #elifdef、审计回归
type: project
---

# chibicc 套件 + sema/PP 修复

## 1. chibicc 套件根因（已闭环）

`make check-chibicc` 曾显示 41 全灭，实为**测试框架 artifact 而非编译器缺陷**：`test/community/chibicc/run.sh` 从脚本目录上跳 5 级推导仓库根，但 sysroot 实际在 `projects/sysroot`，静默传坏 --sysroot 让每个用例链接失败计入 COMPILEFAIL。修复（f05633f，纯脚本）：sysroot 探测顺序 `$MEUOS_SYSROOT → projects/sysroot → sysroot/x86_64 → sysroot`，落空显式 exit 1。

**修正后真实分布：PASS=9 / RUNFAIL=6 / COMPILEFAIL=26**。RUNFAIL 中 bitfield/cast/decl/commonsym 是真实 codegen bug；COMPILEFAIL 分类：A 框架缺口 / B mcc 特性缺口（alloca/asm/\e/##__VA_ARGS__ 等）/ C GNU 宽松性（建议 xfail）/ D 真实 bug。check-chibicc 不在 verify-all 内，仅 `make check-community` 单独触发。

## 2. chibicc B 类两 bug（已修，fd222ad，PASS 9→11）

- **convert() 窄化 cast 不截断**：原 `width <= src->width` 一律 return l，int→char 不做掩码。拆 `<`（IAND 掩码 + 符号扩展）与 `==`（return l）。复现：`(_Bool)(char)256` 应 0。
- **va_end 类型检查过严**：删除 `!typesame(e->type, typeadjvalist)` 检查（chibicc 的 va_end 是 no-op）。
- **坑：TYPEATOMIC 的 u.arith.width 未填充**（mkatomictype 只设 size/align/base）——convert() 顶部对 src/dst 为 TYPEATOMIC 时解包 base。**涉及 mcc 算术转换逻辑时，TYPEATOMIC 的 width/size 必须经 base type 读取。**

## 3. sema/decl E1-E6 修复（0123c96）

- **E1** 命名空间/文件作用域对象重定义（`extern int a; int a;` 仍合法）
- **E4** 非 void 缺 return：`func_falls_off_end()` 块 CFG 可达性分析，`control reaches end`（放 while(1)/死代码，拒绝 `if(x) return` 缺 else）。GNU `__attribute__((noreturn))` 误报修复（db13604）：struct qualtype 增 kind 字段传播 isnoreturn。行号定位（15b6b08）：struct func 增 bodyend 字段
- **E5** 引用未初始化：`reference must be initialized`
- **E6** C++ 禁用 VLA：`variable-length array not allowed`（plain const 长度在 mcc 前端本当作 VLA）

**坑：mcc `struct array.len` 是字节数非元素数**（util.h，arrayaddptr 每次 +8）。用 `w[--work.len]` 按元素下标会段错误，正确取栈顶 `w[work.len/sizeof b - 1]`。遍历一律用 arrayforeach。

## 4. D3-PP #elifdef（已由夹带预修）

`#elifdef` 跳过组误报在基线 b8225ad 已不复现——修复藏在 commit 6ca4ba1（主题是"chibicc run.sh"）夹带引入，测试补于 5bfd08a。**派 T 任务前先花一分钟复现确认缺陷仍存在**；定位改动用 `git log -S "<代码片段>"` 而非提交主题（本仓库多起夹带，主题行不可信）。

**机理（如日后回归）**：pp.c skipbody() 的 TELIFDEF/TELIFNDEF 分支，条件成立时若在 `expectnewline()` 前 return，tok 停在宏名标识符上，调用方 directive() 要求 tok==TNEWLINE，残留标识符被误判报"expected newline"。`#elif` 无此问题。已知未修：`#elifdef` 在 `#else` 之后应报错但 mcc 静默接受。

## 5. 审计回归（已闭环）

c93d5f7（m++ 成员模板）在 expr_postfix.c:288 调 `cpp_pending_record_depth()` 无前置 extern 声明，导致 check-sysroot-static 自举失败。已由 6003f47 修复（顶部 extern 区补声明）。文档：`.issues/0802.md` + `projects/mcc/docs/audit-report.md`。
